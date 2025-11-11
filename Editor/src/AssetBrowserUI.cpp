#include "../include/AssetBrowserUI.h"
#include "imgui.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <memory_resource>

namespace fs = std::filesystem;

namespace EditorWindows {

    static std::pmr::string FileTypes(const fs::path& p, fs::file_status s)
    {
        const std::string name = p.filename().string();

        switch (s.type())
        {
        case fs::file_type::directory:
            ImGui::Text("FILE %s", name.c_str());
            if (ImGui::IsItemClicked()) {
                return std::pmr::string(p.string());
            }
            break;

        case fs::file_type::regular:
            ImGui::Text("REGULAR %s", name.c_str());
            break;

        default:
            ImGui::Text("PENIS %s", name.c_str());
            break;
        }

        return "no response";
    }
    static void DrawBreadcrumbPath(std::string& current_path){
        fs::path path = current_path;

        std::vector<fs::path> parts;
        for (auto& p : path) parts.push_back(p);


        std::string partialPath;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
                partialPath += fs::path::preferred_separator;
            partialPath += parts[i].string();

            std::string folderName = parts[i].string();
            if (folderName.empty()) continue;

            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f,0.2f,0.2f,0.25f));
            if (ImGui::SmallButton(folderName.c_str())) {
                current_path = partialPath;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopID();

            if (i < parts.size() - 1) {
                ImGui::SameLine(0, 1);
                ImGui::Text(">");
                ImGui::SameLine(0, 1);
            }
        }
    }

    void AssetBrowserUI::AssetBrowserWindow() {
        ImGui::Begin("Asset Browser");

        static std::string current_path = fs::current_path().string();

        DrawBreadcrumbPath(current_path);

        //ImGui::Text("Current path: %s", current_path.c_str());
        ImGui::Separator();

        // Up one level
        if (fs::path(current_path).has_parent_path()) {
            ImGui::Text("\\..");
            if (ImGui::IsItemClicked()) {
                current_path = fs::path(current_path).parent_path().string();
                ImGui::End();
                return; // return immediately to avoid iterating invalid path
            }
        }

        for (auto& entry : fs::directory_iterator(current_path))
        {
            std::pmr::string result = FileTypes(entry.path(), entry.symlink_status());
            if (result != "no response") {
                current_path = result; // clicked folder
                break; // stop processing this frame (avoid invalid iterator after path change)
            }
        }

        ImGui::End();
    }

} // namespace EditorWindows