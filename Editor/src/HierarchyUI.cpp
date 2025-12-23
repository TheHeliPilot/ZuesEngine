//
// Created by kukko on 30. 9. 2025.
//

#include "../include/HierarchyUI.h"

#include "Core.h"
#include "imgui.h"
#include "../include/EditorUi.h"
#include "ECS/Component.h"
#include "ECS/HierarchyOutliner.h"

using namespace EditorWindows;

static bool isTreeNodeHovered = false;
static EntityID lastRightClickedEntity;

void GenerateHierarchyItems()
{
   static ImGuiTreeNodeFlags base_flags =
         ImGuiTreeNodeFlags_DrawLinesToNodes |
         ImGuiTreeNodeFlags_DefaultOpen |
         ImGuiTreeNodeFlags_OpenOnArrow |
         ImGuiTreeNodeFlags_NavLeftJumpsToParent;

   auto hierarchyItems = Engine::ECS::Hierarchy::GetFlattenedHierarchy();

   int prevLevel = -1;
   int openNodeCount = 0; // only counts nodes actually pushed
   int skipLevel = -1;    // if >=0, skip all nodes deeper than this

   isTreeNodeHovered = false;

   for (int i = 0; i < hierarchyItems.size(); i++)
   {
      if (hierarchyItems[i].id.id == 0)
         continue;

      int level = hierarchyItems[i].depth;
      bool selected = EditorUi::IsEntitySelected(hierarchyItems[i].id);

      // Skip children of a closed parent
      if (skipLevel >= 0 && level > skipLevel)
         continue;

      // Close nodes if moving up the hierarchy
      while (prevLevel > level && openNodeCount > 0)
      {
         ImGui::TreePop();
         openNodeCount--;
         prevLevel--;
      }

      bool hasChild = (i + 1 < hierarchyItems.size() && hierarchyItems[i + 1].depth > level);

      ImGuiTreeNodeFlags node_flags = base_flags;
      if (!hasChild)
         node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

      // Check if entity has unknown components (broken entity)
      World* world = Engine::Core::GetCurrentWorld();
      bool hasUnknownComponents = world && world->EntityHasUnknownComponents(hierarchyItems[i].id);

      // Apply red text color for broken entities
      if (hasUnknownComponents) {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
      }

      // Render node
      bool open = ImGui::TreeNodeEx((void*)(intptr_t)hierarchyItems[i].id.id,
                                    node_flags, "%s", hierarchyItems[i].id.name.c_str());

      // Pop red text color
      if (hasUnknownComponents) {
         ImGui::PopStyleColor();
      }

      if (selected)
      {
         ImDrawList* drawList = ImGui::GetWindowDrawList();
         ImVec2 min = ImGui::GetItemRectMin();
         ImVec2 max = ImGui::GetItemRectMax();

         // Set max X to window width
         max.x = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();

         drawList->AddRect(min, max, IM_COL32(150, 150, 150, 255), 0.0f, 0, 1.0f);
      }

      if (ImGui::BeginDragDropSource())
      {
         ImGui::SetDragDropPayload("HIERARCHY_ITEM", &hierarchyItems[i].id, sizeof(int));
         ImGui::Text(("Gameobject " + std::to_string(hierarchyItems[i].id.id)).c_str());
         ImGui::EndDragDropSource();
      }

      if (ImGui::IsItemHovered())
         isTreeNodeHovered = true;

      if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         lastRightClickedEntity = hierarchyItems[i].id;

      if (ImGui::BeginDragDropTarget())
      {
         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ITEM"))
         {
            const auto payloadData = static_cast<EntityID*>(payload->Data);

            bool canBeDropped = true;
            EntityID currentEntity = hierarchyItems[i].id;
            while (currentEntity != NullEntityID())
            {
               EntityID parent = Engine::Core::GetCurrentWorld()->GetComponent<Engine::ECS::Component::TransformComponent>(currentEntity).parent;

               if (currentEntity.id == payloadData->id)
                  canBeDropped = false;

               currentEntity = parent;
            }

            if (canBeDropped)
            {
               Engine::Core::GetCurrentWorld()->GetComponent<Engine::ECS::Component::TransformComponent>(*payloadData).parent = hierarchyItems[i].id;
               Engine::ECS::Hierarchy::BuildCache(Engine::Core::GetCurrentWorld());
               EditorUi::MarkWorldAsModified();
            }
         }
         ImGui::EndDragDropTarget();
      }

      // TODO: Multiselect Movement fix
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
      {
         if (Engine::Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || Engine::Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT))
         {
            EditorUi::selectedEntities.clear();
            EditorUi::selectedEntities.push_back(hierarchyItems[i].id);
         }
         else
         {
            EditorUi::selectedEntities.clear();
            EditorUi::selectedEntities.push_back(hierarchyItems[i].id);
         }

      }

      if (hasChild)
      {
         if (!open)
         {
            // Parent closed → skip its children
            skipLevel = level;
         }
         else
         {
            // Parent open → children visible
            skipLevel = -1;
            openNodeCount++;
         }
      }

      // Leaf nodes: nothing to push, skipLevel unaffected
      prevLevel = level;
   }

   // Safely close any remaining open nodes
   while (openNodeCount > 0)
   {
      ImGui::TreePop();
      openNodeCount--;
   }
}


void HierarchyUI::HierarchyWindow()
{
   ImGui::Begin("Hierarchy");

   // Display world name with unsaved indicator
   std::string worldDisplayName = EditorUi::currentWorldName;
   if (EditorUi::isWorldUnsaved) {
      worldDisplayName += " *";
   }

   // Draw world name as a tree root
   ImGuiTreeNodeFlags worldNodeFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_DrawLinesToNodes |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;

   bool worldNodeOpen = ImGui::TreeNodeEx("##WorldRoot", worldNodeFlags, "%s", worldDisplayName.c_str());

   if (worldNodeOpen) {
      Engine::ECS::Hierarchy::BuildCache(Engine::Core::GetCurrentWorld());
      GenerateHierarchyItems();
      ImGui::TreePop();
   }

   ImVec2 availableSpace = ImGui::GetContentRegionAvail();
   // Only create the invisible button if we have valid space (prevents zero-size assertion)
   if (availableSpace.x > 1.0f && availableSpace.y > 1.0f)
   {
      if (ImGui::InvisibleButton("##background_drop", availableSpace))
      {
         if (EditorUi::MouseInWindow("Hierarchy") && !EditorUi::selectedEntities.empty())
         {
            EditorUi::selectedEntities.clear();
         }
      }
   }

   if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
   {
      if (ImGui::BeginDragDropTarget())
      {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ITEM", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
               const auto payloadData = static_cast<EntityID*>(payload->Data);

               Engine::Core::GetCurrentWorld()->GetComponent<Engine::ECS::Component::TransformComponent>(*payloadData).parent = NullEntityID();
               Engine::ECS::Hierarchy::BuildCache(Engine::Core::GetCurrentWorld());
               EditorUi::MarkWorldAsModified();

         }
         ImGui::EndDragDropTarget();
      }
   }

   if (EditorUi::MouseInWindow("Hierarchy") && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !isTreeNodeHovered)
      ImGui::OpenPopup("hierarchy_right_click");

   World* world = Engine::Core::GetCurrentWorld();

   if (ImGui::BeginPopup("hierarchy_right_click")) {
      if (ImGui::Selectable("Create Empty"))
      {
         const EntityID emptyEntity = world->CreateEntity("Empty Entity");
         world->AddComponent<Engine::ECS::Component::TransformComponent>(emptyEntity, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f
        });
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::Selectable("Create Sprite"))
      {
         const EntityID spriteEntity = world->CreateEntity("Sprite Entity");
         world->AddComponent<Engine::ECS::Component::TransformComponent>(spriteEntity, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f
        });
         world->AddComponent<Engine::ECS::Component::SpriteComponent>(spriteEntity, {
            .spriteName = "",
            .size = {2.0f, 2.0f}, // Using Sprite size for visual scale
            .color = {1.0f, 0.0f, 0.0f, 1.0f}, // Red
        });
         EditorUi::MarkWorldAsModified();
      }
      ImGui::EndPopup();
   }

   //EditorUi::MouseInWindow("Hierarchy") pridane aby sa to neotvaralo mimo hierarchie
   if (EditorUi::MouseInWindow("Hierarchy") && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && isTreeNodeHovered)
   {
      ImGui::OpenPopup("hierarchy_right_click_item");
   }


   if (ImGui::BeginPopup("hierarchy_right_click_item"))
   {
      if (ImGui::Selectable("Create Child Empty"))
      {
         const EntityID emptyChildEntity = world->CreateEntity("Child Empty Entity");
         world->AddComponent<Engine::ECS::Component::TransformComponent>(emptyChildEntity, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f,
            .parent = lastRightClickedEntity
        });
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::Selectable("Create Child Sprite"))
      {
         const EntityID childEntitySprite = world->CreateEntity("Child Empty Entity");
         world->AddComponent<Engine::ECS::Component::TransformComponent>(childEntitySprite, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f,
            .parent = lastRightClickedEntity
        });
         world->AddComponent<Engine::ECS::Component::SpriteComponent>(childEntitySprite, {
             .spriteName = "",
             .size = {2.0f, 2.0f}, // Using Sprite size for visual scale
             .color = {1.0f, 0.0f, 0.0f, 1.0f}, // Red
         });
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::Selectable("Create Parent Empty"))
      {
         const EntityID parentEmptyEntity = world->CreateEntity("Parent Empty Entity");
         world->AddComponent<Engine::ECS::Component::TransformComponent>(parentEmptyEntity, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f,
            .parent = world->GetComponent<Engine::ECS::Component::TransformComponent>(lastRightClickedEntity).parent
        });

         world->GetComponent<Engine::ECS::Component::TransformComponent>(lastRightClickedEntity).parent = parentEmptyEntity;
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::Selectable("Create Parent Sprite"))
      {
         const EntityID parentSpriteEntity = world->CreateEntity("Parent Sprite Entity");
         world->AddComponent<Engine::ECS::Component::TransformComponent>(parentSpriteEntity, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f,
            .parent = world->GetComponent<Engine::ECS::Component::TransformComponent>(lastRightClickedEntity).parent
        });
         world->AddComponent<Engine::ECS::Component::SpriteComponent>(parentSpriteEntity, {
             .spriteName = "",
             .size = {2.0f, 2.0f}, // Using Sprite size for visual scale
             .color = {1.0f, 0.0f, 0.0f, 1.0f}, // Red
         });

         world->GetComponent<Engine::ECS::Component::TransformComponent>(lastRightClickedEntity).parent = parentSpriteEntity;
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::Selectable("Delete"))
      {
         world->DestroyEntity(lastRightClickedEntity);
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::Selectable("Rename"))
      {
         ImGui::OpenPopup("rename_entity_popup");
      }
      ImGui::EndPopup();
   }

   // Rename entity popup
   if (ImGui::BeginPopupModal("rename_entity_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
   {
      static char entityNameBuffer[256] = "";
      static bool firstOpen = true;

      // Initialize the buffer with the current entity name when first opened
      if (firstOpen && lastRightClickedEntity.IsValid())
      {
         strncpy(entityNameBuffer, lastRightClickedEntity.name.c_str(), sizeof(entityNameBuffer) - 1);
         entityNameBuffer[sizeof(entityNameBuffer) - 1] = '\0';
         firstOpen = false;
         ImGui::SetKeyboardFocusHere();
      }

      ImGui::Text("Enter new name:");
      if (ImGui::InputText("##EntityName", entityNameBuffer, sizeof(entityNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
      {
         // Update entity name
         lastRightClickedEntity.name = std::string(entityNameBuffer);
         EditorUi::MarkWorldAsModified();
         firstOpen = true;
         ImGui::CloseCurrentPopup();
      }

      if (ImGui::Button("OK", ImVec2(120, 0)))
      {
         lastRightClickedEntity.name = std::string(entityNameBuffer);
         EditorUi::MarkWorldAsModified();
         firstOpen = true;
         ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0)))
      {
         firstOpen = true;
         ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
   }

   ImGui::End();
}