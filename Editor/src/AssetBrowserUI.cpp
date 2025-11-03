#include "../include/AssetBrowserUI.h"
#include <functional>
#include "imgui.h"
#include "imgui_internal.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <chrono>

#include "../include/TextureCutterUI.h"

namespace EditorWindows {

    void AssetBrowserUI::AssetBrowserWindow() {
        ImGui::Begin("Asset Browser");



        ImGui::End();
    }

} // namespace EditorWindows