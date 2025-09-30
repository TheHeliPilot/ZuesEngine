//
// Created by kukko on 30. 9. 2025.
//

#include "HierarchyUI.h"

#include "imgui.h"
#include "ECS/Component.h"
#include "ECS/HierarchyOutliner.h"

using namespace EditorWindows;

void GenerateHierarchyItems()
{
   static ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
   auto hierarchyItems = Engine::ECS::Hierarchy::GetFlattenedHierarchy();
   int noParent = 0;

   for (int i = 0; i < hierarchyItems.size(); i++)
   {
      int skips = 0;
      std::vector<int> collection;

      if (hierarchyItems[i].depth == noParent && skips == 0)
      {
         for (int j = i +1; j < hierarchyItems.size(); j++)
         {
            if (hierarchyItems[j].depth != noParent)
            {
               collection.push_back(1);
               skips++;
            }

         }
      }
      else
      {

      }
   }
}


void HierarchyUI::HierarchyWindow()
{
   ImGui::Begin("Hierarchy");

   static ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
   if (ImGui::TreeNodeEx("Parent", base_flags))
   {
      if (ImGui::TreeNodeEx("Child 1", base_flags))
      {
         ImGui::TreePop();
      }
      if (ImGui::TreeNodeEx("Child 2", base_flags))
      {
         ImGui::TreePop();
      }
      ImGui::TreePop();
   }

   GenerateHierarchyItems();

   ImGui::End();
}
