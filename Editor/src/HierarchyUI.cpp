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

void GenerateHierarchyItems()
{
   static ImGuiTreeNodeFlags base_flags =
       ImGuiTreeNodeFlags_DrawLinesToNodes |
       ImGuiTreeNodeFlags_DefaultOpen |
       ImGuiTreeNodeFlags_OpenOnArrow;

   auto hierarchyItems = Engine::ECS::Hierarchy::GetFlattenedHierarchy();

   int prevLevel = -1;
   int openNodeCount = 0; // only counts nodes actually pushed
   int skipLevel = -1;    // if >=0, skip all nodes deeper than this

   for (int i = 0; i < hierarchyItems.size(); i++)
   {
      int level = hierarchyItems[i].depth;

      // Skip children of a closed parent
      if (skipLevel >= 0 && level > skipLevel)
         continue;

      // Close nodes if moving up the hierarchy
      while (prevLevel >= level && openNodeCount > 0)
      {
         ImGui::TreePop();
         openNodeCount--;
         prevLevel--;
      }

      bool hasChild = (i + 1 < hierarchyItems.size() && hierarchyItems[i + 1].depth > level);

      ImGuiTreeNodeFlags node_flags = base_flags;
      if (!hasChild)
         node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

      // Render node
      bool open = ImGui::TreeNodeEx((void*)(intptr_t)hierarchyItems[i].id.id,
                                    node_flags, "%d", i);

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

   Engine::ECS::Hierarchy::BuildCache(Engine::Core::GetCurrentWorld());
   GenerateHierarchyItems();

   if (EditorUi::MouseInWindow("Hierarchy") && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
      ImGui::OpenPopup("hierarchy_right_click");

   if (ImGui::BeginPopup("hierarchy_right_click")) {
      if (ImGui::Selectable("Create Object")) { /* action */ }
      if (ImGui::Selectable("Create Sprite")) { /* action */ }
      ImGui::EndPopup();
   }
   //EditorUi::MouseInWindow("Hierarchy") pridane aby sa to neotvaralo mimo hierarchie
   if (EditorUi::MouseInWindow("Hierarchy") && ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      ImGui::OpenPopup("hierarchy_right_click_item");

   if (ImGui::BeginPopup("hierarchy_right_click_item"))
   {
      if (ImGui::Selectable("Create Child Object")) { /* action */ }
      if (ImGui::Selectable("Create Parent Object")) { /* action */ }
      if (ImGui::Selectable("Delete")) { /* action */ }
      ImGui::EndPopup();
   }

   ImGui::End();
}
