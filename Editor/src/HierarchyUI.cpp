//
// Created by kukko on 30. 9. 2025.
//

#include "../include/HierarchyUI.h"

#include <GLFW/glfw3.h>

#include "Core.h"
#include "imgui.h"
#include "../include/EditorUi.h"
#include "ECS/Component.h"
#include "ECS/HierarchyOutliner.h"

using namespace EditorWindows;

static bool isTreeNodeHovered = false;
static EntityID lastRightClickedEntity;
static EntityID draggedEntity;  // Track what's being dragged for drop indicator
static bool showDropIndicator = false;
static float dropIndicatorY = 0.0f;

// Helper to check if an entity is an ancestor of another
static bool IsAncestor(World* world, EntityID potentialAncestor, EntityID entity) {
   if (!world || !entity.IsValid()) return false;

   EntityID current = entity;
   while (current.IsValid()) {
      if (!world->HasComponent<Engine::ECS::Component::TransformComponent>(current))
         break;
      EntityID parent = world->GetComponent<Engine::ECS::Component::TransformComponent>(current).parent;
      if (parent.id == potentialAncestor.id)
         return true;
      current = parent;
   }
   return false;
}

void GenerateHierarchyItems()
{
   static ImGuiTreeNodeFlags base_flags =
         ImGuiTreeNodeFlags_OpenOnArrow |
         ImGuiTreeNodeFlags_OpenOnDoubleClick |
         ImGuiTreeNodeFlags_SpanAvailWidth;

   auto hierarchyItems = Engine::ECS::Hierarchy::GetFlattenedHierarchy();
   World* world = Engine::Core::GetCurrentWorld();

   int prevLevel = -1;
   int openNodeCount = 0;
   int skipLevel = -1;

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
      if (selected)
         node_flags |= ImGuiTreeNodeFlags_Selected;

      // Check if entity has unknown components (broken entity)
      bool hasUnknownComponents = world && world->EntityHasUnknownComponents(hierarchyItems[i].id);

      // Apply color based on state
      bool colorPushed = false;
      if (hasUnknownComponents) {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
         colorPushed = true;
      }

      // Render node
      bool open = ImGui::TreeNodeEx((void*)(intptr_t)hierarchyItems[i].id.id,
                                    node_flags, "%s", hierarchyItems[i].id.name.c_str());

      if (colorPushed) {
         ImGui::PopStyleColor();
      }

      // Tooltip for broken entities
      if (hasUnknownComponents && ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Entity has unknown components.\nLoad the Game DLL to restore them.");
      }

      // --- Drag Source ---
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
      {
         draggedEntity = hierarchyItems[i].id;
         ImGui::SetDragDropPayload("HIERARCHY_ITEM", &hierarchyItems[i].id, sizeof(EntityID));
         ImGui::Text("%s", hierarchyItems[i].id.name.c_str());
         ImGui::EndDragDropSource();
      }

      if (ImGui::IsItemHovered())
         isTreeNodeHovered = true;

      if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         lastRightClickedEntity = hierarchyItems[i].id;

      // --- Drag Target (drop ON this item to make it a child) ---
      if (ImGui::BeginDragDropTarget())
      {
         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ITEM"))
         {
            const auto payloadData = static_cast<EntityID*>(payload->Data);

            // Check if we can drop (not dropping on self or own descendant)
            bool canBeDropped = payloadData->id != hierarchyItems[i].id.id &&
                               !IsAncestor(world, *payloadData, hierarchyItems[i].id);

            if (canBeDropped && world)
            {
               world->GetComponent<Engine::ECS::Component::TransformComponent>(*payloadData).parent = hierarchyItems[i].id;
               Engine::ECS::Hierarchy::BuildCache(world);
               EditorUi::MarkWorldAsModified();
            }
         }
         ImGui::EndDragDropTarget();
      }

      // Selection handling
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
      {
         if (Engine::Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || Engine::Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL))
         {
            // Toggle selection with Ctrl
            auto it = std::find_if(EditorUi::selectedEntities.begin(), EditorUi::selectedEntities.end(),
               [&](const EntityID& e) { return e.id == hierarchyItems[i].id.id; });
            if (it != EditorUi::selectedEntities.end()) {
               EditorUi::selectedEntities.erase(it);
            } else {
               EditorUi::selectedEntities.push_back(hierarchyItems[i].id);
            }
         }
         else
         {
            // Single select
            EditorUi::selectedEntities.clear();
            EditorUi::selectedEntities.push_back(hierarchyItems[i].id);
         }
      }

      if (hasChild)
      {
         if (!open)
         {
            skipLevel = level;
         }
         else
         {
            skipLevel = -1;
            openNodeCount++;
         }
      }

      prevLevel = level;
   }

   // Close any remaining open nodes
   while (openNodeCount > 0)
   {
      ImGui::TreePop();
      openNodeCount--;
   }
}


void HierarchyUI::HierarchyWindow()
{
   ImGui::Begin("Hierarchy");

   World* world = Engine::Core::GetCurrentWorld();
   if (!world) {
      ImGui::Text("No active world");
      ImGui::End();
      return;
   }

   // --- World Header (styled, not a tree node) ---
   {
      std::string worldDisplayName = EditorUi::currentWorldName;
      if (EditorUi::isWorldUnsaved) {
         worldDisplayName += " *";
      }

      // Header styling
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.35f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.3f, 0.4f, 1.0f));

      // Selectable header for the world
      ImGui::Selectable(worldDisplayName.c_str(), false, ImGuiSelectableFlags_None);

      ImGui::PopStyleColor(3);

      // Drop target on world header to unparent entities (make them root)
      if (ImGui::BeginDragDropTarget())
      {
         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ITEM"))
         {
            const auto payloadData = static_cast<EntityID*>(payload->Data);
            if (world) {
               world->GetComponent<Engine::ECS::Component::TransformComponent>(*payloadData).parent = NullEntityID();
               Engine::ECS::Hierarchy::BuildCache(world);
               EditorUi::MarkWorldAsModified();
            }
         }
         ImGui::EndDragDropTarget();
      }

      ImGui::Separator();
   }

   // --- Entity Hierarchy ---
   Engine::ECS::Hierarchy::BuildCache(world);
   GenerateHierarchyItems();

   // --- Background area for deselection and drop-to-root ---
   ImVec2 availableSpace = ImGui::GetContentRegionAvail();
   if (availableSpace.x > 1.0f && availableSpace.y > 1.0f)
   {
      // Invisible button for click detection
      ImGui::InvisibleButton("##hierarchy_background", availableSpace);

      // Click on empty space deselects
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && EditorUi::MouseInWindow("Hierarchy"))
      {
         EditorUi::selectedEntities.clear();
      }

      // Drop on empty space makes entity root-level
      if (ImGui::BeginDragDropTarget())
      {
         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ITEM"))
         {
            const auto payloadData = static_cast<EntityID*>(payload->Data);
            if (world) {
               world->GetComponent<Engine::ECS::Component::TransformComponent>(*payloadData).parent = NullEntityID();
               Engine::ECS::Hierarchy::BuildCache(world);
               EditorUi::MarkWorldAsModified();
            }
         }
         ImGui::EndDragDropTarget();
      }
   }

   // --- Context Menus ---

   // Right-click on empty space
   if (EditorUi::MouseInWindow("Hierarchy") && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !isTreeNodeHovered)
      ImGui::OpenPopup("hierarchy_context_menu");

   if (ImGui::BeginPopup("hierarchy_context_menu")) {
      ImGui::TextDisabled("Create Entity");
      ImGui::Separator();

      if (ImGui::MenuItem("Empty Entity"))
      {
         const EntityID emptyEntity = world->CreateEntity("Empty Entity");
         EditorUi::selectedEntities.clear();
         EditorUi::selectedEntities.push_back(emptyEntity);
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::MenuItem("Sprite"))
      {
         const EntityID spriteEntity = world->CreateEntity("Sprite");
         world->AddComponent<Engine::ECS::Component::SpriteComponent>(spriteEntity, {
            .spriteName = "",
            .size = {1.0f, 1.0f},
            .color = {1.0f, 1.0f, 1.0f, 1.0f},
         });
         EditorUi::selectedEntities.clear();
         EditorUi::selectedEntities.push_back(spriteEntity);
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::MenuItem("Camera"))
      {
         const EntityID cameraEntity = world->CreateEntity("Camera");
         world->AddComponent<Engine::ECS::Component::CameraComponent>(cameraEntity, {
            .halfHeight = 10.0f
         });
         EditorUi::selectedEntities.clear();
         EditorUi::selectedEntities.push_back(cameraEntity);
         EditorUi::MarkWorldAsModified();
      }
      if (ImGui::MenuItem("Text"))
      {
         const EntityID textEntity = world->CreateEntity("Text");
         world->AddComponent<Engine::ECS::Component::TextComponent>(textEntity, {
            .text = "Hello World",
            .color = {1.0f, 1.0f, 1.0f, 1.0f},
            .scale = 1.0f
         });
         EditorUi::selectedEntities.clear();
         EditorUi::selectedEntities.push_back(textEntity);
         EditorUi::MarkWorldAsModified();
      }
      ImGui::EndPopup();
   }

   // Right-click on entity
   if (EditorUi::MouseInWindow("Hierarchy") && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && isTreeNodeHovered)
   {
      ImGui::OpenPopup("entity_context_menu");
   }

   if (ImGui::BeginPopup("entity_context_menu"))
   {
      ImGui::TextDisabled("%s", lastRightClickedEntity.name.c_str());
      ImGui::Separator();

      if (ImGui::BeginMenu("Create Child"))
      {
         if (ImGui::MenuItem("Empty"))
         {
            const EntityID childEntity = world->CreateEntity("Empty");
            world->GetComponent<Engine::ECS::Component::TransformComponent>(childEntity).parent = lastRightClickedEntity;
            Engine::ECS::Hierarchy::BuildCache(world);
            EditorUi::selectedEntities.clear();
            EditorUi::selectedEntities.push_back(childEntity);
            EditorUi::MarkWorldAsModified();
         }
         if (ImGui::MenuItem("Sprite"))
         {
            const EntityID childEntity = world->CreateEntity("Sprite");
            world->GetComponent<Engine::ECS::Component::TransformComponent>(childEntity).parent = lastRightClickedEntity;
            world->AddComponent<Engine::ECS::Component::SpriteComponent>(childEntity, {
               .spriteName = "",
               .size = {1.0f, 1.0f},
               .color = {1.0f, 1.0f, 1.0f, 1.0f},
            });
            Engine::ECS::Hierarchy::BuildCache(world);
            EditorUi::selectedEntities.clear();
            EditorUi::selectedEntities.push_back(childEntity);
            EditorUi::MarkWorldAsModified();
         }
         ImGui::EndMenu();
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Rename", "F2"))
      {
         ImGui::OpenPopup("rename_entity_popup");
      }

      if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
      {
         // TODO: Implement entity duplication
      }

      if (ImGui::MenuItem("Unparent"))
      {
         world->GetComponent<Engine::ECS::Component::TransformComponent>(lastRightClickedEntity).parent = NullEntityID();
         Engine::ECS::Hierarchy::BuildCache(world);
         EditorUi::MarkWorldAsModified();
      }

      ImGui::Separator();

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
      if (ImGui::MenuItem("Delete", "Del"))
      {
         world->DestroyEntity(lastRightClickedEntity);
         // Remove from selection if selected
         auto it = std::find_if(EditorUi::selectedEntities.begin(), EditorUi::selectedEntities.end(),
            [](const EntityID& e) { return e.id == lastRightClickedEntity.id; });
         if (it != EditorUi::selectedEntities.end()) {
            EditorUi::selectedEntities.erase(it);
         }
         EditorUi::MarkWorldAsModified();
      }
      ImGui::PopStyleColor();

      ImGui::EndPopup();
   }

   // Rename popup (modal)
   static bool openRenamePopup = false;
   static char entityNameBuffer[256] = "";
   static bool firstOpenRename = true;

   if (ImGui::IsPopupOpen("rename_entity_popup") || openRenamePopup)
   {
      if (!ImGui::IsPopupOpen("rename_entity_popup_modal"))
      {
         if (lastRightClickedEntity.IsValid()) {
            strncpy(entityNameBuffer, lastRightClickedEntity.name.c_str(), sizeof(entityNameBuffer) - 1);
            entityNameBuffer[sizeof(entityNameBuffer) - 1] = '\0';
            firstOpenRename = true;
         }
         ImGui::OpenPopup("rename_entity_popup_modal");
         openRenamePopup = false;
      }
   }

   if (ImGui::BeginPopupModal("rename_entity_popup_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
   {
      ImGui::Text("Rename Entity");
      ImGui::Separator();

      if (firstOpenRename) {
         ImGui::SetKeyboardFocusHere();
         firstOpenRename = false;
      }

      bool enterPressed = ImGui::InputText("##rename_input", entityNameBuffer, sizeof(entityNameBuffer),
         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

      ImGui::Spacing();

      if (ImGui::Button("OK", ImVec2(80, 0)) || enterPressed)
      {
         if (strlen(entityNameBuffer) > 0 && world) {
            world->RenameEntity(lastRightClickedEntity, std::string(entityNameBuffer));
            EditorUi::MarkWorldAsModified();
         }
         ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
      {
         ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
   }

   // Keyboard shortcuts
   if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
   {
      // Delete key to delete selected entities
      if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !EditorUi::selectedEntities.empty())
      {
         for (const auto& entity : EditorUi::selectedEntities)
         {
            world->DestroyEntity(entity);
         }
         EditorUi::selectedEntities.clear();
         EditorUi::MarkWorldAsModified();
      }

      // F2 to rename
      if (ImGui::IsKeyPressed(ImGuiKey_F2) && EditorUi::selectedEntities.size() == 1)
      {
         lastRightClickedEntity = EditorUi::selectedEntities[0];
         openRenamePopup = true;
      }
   }

   ImGui::End();
}
