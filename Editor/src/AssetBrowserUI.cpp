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

    void AssetBrowserUI::AssetBrowserWindow() {
        ImGui::Begin("Asset Browser");

        static std::string current_path = fs::current_path().string(); // persistent between frames

        ImGui::Text("Current path: %s", current_path.c_str());
        ImGui::Separator();

        // Up one level

        if (fs::path(current_path).has_parent_path()) {
            ImGui::Text("/..");
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