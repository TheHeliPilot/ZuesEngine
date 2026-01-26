#include "../include/AssetBrowserUI.h"
#include "../include/TextureCutterUI.h"
#include "imgui.h"
#include <algorithm>
#include <Engine/TextureManager.h>
#include <Engine/SpriteMetaFile.h>
#include <Engine/EngineDefines.h>
#include <glad/glad.h>
#include <stb/stb_image.h>

namespace fs = std::filesystem;

namespace EditorWindows {

    // Static member initialization
    float AssetBrowserUI::thumbnailSize = 80.0f;
    float AssetBrowserUI::padding = 16.0f;
    bool AssetBrowserUI::showHiddenFiles = false;
    float AssetBrowserUI::folderTreeWidth = 200.0f;

    std::string AssetBrowserUI::s_CurrentPath;
    std::string AssetBrowserUI::s_AssetsRoot;
    std::vector<AssetEntry> AssetBrowserUI::s_CurrentEntries;
    std::unordered_map<std::string, uint32_t> AssetBrowserUI::s_ThumbnailCache;
    int AssetBrowserUI::s_SelectedIndex = -1;
    bool AssetBrowserUI::s_NeedsRefresh = true;

    // Static for pending world load after save prompt
    static std::string s_PendingWorldToLoad;

    std::string AssetBrowserUI::GetAssetsRoot() {
        if (s_AssetsRoot.empty()) {
            if (!EditorUi::projectDir.empty()) {
                const fs::path assetsPath = EditorUi::projectDir / "Assets";
                if (!fs::exists(assetsPath)) {
                    try {
                        fs::create_directories(assetsPath);
                        LOG_INFO("Created Assets folder: " + assetsPath.string());
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to create Assets folder: " + std::string(e.what()));
                        s_AssetsRoot = EditorUi::projectDir.generic_string();
                        return s_AssetsRoot;
                    }
                }
                s_AssetsRoot = assetsPath.generic_string();
            } else {
                s_AssetsRoot = fs::current_path().string();
            }
        }
        return s_AssetsRoot;
    }

    bool AssetBrowserUI::CanNavigateUp() {
        if (s_CurrentPath.empty()) return false;

        fs::path current(s_CurrentPath);
        fs::path root(GetAssetsRoot());

        current = fs::weakly_canonical(current);
        root = fs::weakly_canonical(root);

        return current != root && current.generic_string().find(root.generic_string()) == 0;
    }

    void AssetBrowserUI::AssetBrowserWindow() {
        if (!ImGui::Begin("Asset Browser", &EditorUi::showAssetBrowser)) {
            ImGui::End();
            return;
        }

        // Initialize with Assets folder
        if (s_CurrentPath.empty()) {
            s_CurrentPath = GetAssetsRoot();
            s_NeedsRefresh = true;
        }

        // Refresh if needed
        if (s_NeedsRefresh) {
            RefreshDirectory();
            s_NeedsRefresh = false;
        }

        DrawToolbar();
        ImGui::Separator();

        // Main content area with folder tree on left and grid on right
        float contentHeight = ImGui::GetContentRegionAvail().y;

        // Left panel - Folder tree
        ImGui::BeginChild("FolderTree", ImVec2(folderTreeWidth, contentHeight), true);
        DrawFolderTree();
        ImGui::EndChild();

        ImGui::SameLine();

        // Splitter
        ImGui::InvisibleButton("##Splitter", ImVec2(4.0f, contentHeight));
        if (ImGui::IsItemActive()) {
            folderTreeWidth += ImGui::GetIO().MouseDelta.x;
            folderTreeWidth = std::clamp(folderTreeWidth, 100.0f, 400.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        ImGui::SameLine();

        // Right panel - Asset grid
        ImGui::BeginChild("RightPanel", ImVec2(0, contentHeight), false);
        DrawBreadcrumbs();
        ImGui::Separator();
        DrawAssetGrid();
        ImGui::EndChild();

        // Handle "Save Before Loading" popup
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Save Before Loading?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Current world has unsaved changes.");
            ImGui::Text("Do you want to save before loading '%s'?", s_PendingWorldToLoad.c_str());
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(80, 0))) {
                EditorUi::SaveWorld();
                EditorUi::LoadWorld(s_PendingWorldToLoad);
                s_PendingWorldToLoad.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(80, 0))) {
                EditorUi::LoadWorld(s_PendingWorldToLoad);
                s_PendingWorldToLoad.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                s_PendingWorldToLoad.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void AssetBrowserUI::DrawFolderTree() {
        fs::path assetsRoot(GetAssetsRoot());

        // Draw "Assets" root node
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;

        // Check if current path is the root
        fs::path currentNorm = fs::weakly_canonical(fs::path(s_CurrentPath));
        fs::path rootNorm = fs::weakly_canonical(assetsRoot);
        if (currentNorm == rootNorm) {
            rootFlags |= ImGuiTreeNodeFlags_Selected;
        }

        bool rootOpen = ImGui::TreeNodeEx("Assets", rootFlags);

        // Click on root to navigate
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            s_CurrentPath = GetAssetsRoot();
            s_NeedsRefresh = true;
        }

        if (rootOpen) {
            DrawFolderTreeNode(assetsRoot);
            ImGui::TreePop();
        }
    }

    void AssetBrowserUI::DrawFolderTreeNode(const std::filesystem::path& folderPath) {
        try {
            // Collect and sort subdirectories
            std::vector<fs::path> subdirs;
            for (const auto& entry : fs::directory_iterator(folderPath)) {
                if (entry.is_directory()) {
                    std::string name = entry.path().filename().string();
                    // Skip hidden folders
                    if (!showHiddenFiles && !name.empty() && name[0] == '.') {
                        continue;
                    }
                    subdirs.push_back(entry.path());
                }
            }

            std::sort(subdirs.begin(), subdirs.end(), [](const fs::path& a, const fs::path& b) {
                return a.filename().string() < b.filename().string();
            });

            for (const auto& subdir : subdirs) {
                std::string folderName = subdir.filename().string();

                // Check if this folder has subfolders
                bool hasSubfolders = false;
                try {
                    for (const auto& entry : fs::directory_iterator(subdir)) {
                        if (entry.is_directory()) {
                            hasSubfolders = true;
                            break;
                        }
                    }
                } catch (...) {}

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (!hasSubfolders) {
                    nodeFlags |= ImGuiTreeNodeFlags_Leaf;
                }

                // Check if this is the selected folder
                fs::path subdirNorm = fs::weakly_canonical(subdir);
                fs::path currentNorm = fs::weakly_canonical(fs::path(s_CurrentPath));
                if (subdirNorm == currentNorm) {
                    nodeFlags |= ImGuiTreeNodeFlags_Selected;
                }

                ImGui::PushID(folderName.c_str());
                bool nodeOpen = ImGui::TreeNodeEx(folderName.c_str(), nodeFlags);

                // Click to navigate
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    s_CurrentPath = subdir.generic_string();
                    s_NeedsRefresh = true;
                }

                if (nodeOpen) {
                    if (hasSubfolders) {
                        DrawFolderTreeNode(subdir);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        } catch (const std::exception& e) {
            // Silently ignore permission errors
        }
    }

    void AssetBrowserUI::RefreshDirectory() {
        s_CurrentEntries.clear();
        s_SelectedIndex = -1;

        if (!fs::exists(s_CurrentPath) || !fs::is_directory(s_CurrentPath)) {
            s_CurrentPath = GetAssetsRoot();
            if (!fs::exists(s_CurrentPath)) {
                return;
            }
        }

        try {
            for (const auto& entry : fs::directory_iterator(s_CurrentPath)) {
                const std::string filename = entry.path().filename().string();

                if (!showHiddenFiles && !filename.empty() && filename[0] == '.') {
                    continue;
                }

                // Skip .sprite.meta files
                if (filename.ends_with(".sprite.meta")) {
                    continue;
                }

                AssetEntry asset;
                asset.path = entry.path();
                asset.name = filename;
                asset.type = DetermineAssetType(entry.path());

                // Check for sprite meta file
                if (asset.type == AssetType::Image) {
                    fs::path metaPath = Engine::SpriteMetaFile::GetMetaFilePath(entry.path());
                    if (fs::exists(metaPath)) {
                        asset.hasMeta = true;
                        Engine::SpriteMetaFile meta;
                        if (meta.Load(metaPath)) {
                            asset.spriteCount = static_cast<int>(meta.sprites.size());
                        }
                    }
                }

                s_CurrentEntries.push_back(asset);
            }

            // Sort: folders first, then alphabetically
            std::sort(s_CurrentEntries.begin(), s_CurrentEntries.end(),
                [](const AssetEntry& a, const AssetEntry& b) {
                    if (a.type == AssetType::Folder && b.type != AssetType::Folder) return true;
                    if (a.type != AssetType::Folder && b.type == AssetType::Folder) return false;
                    return a.name < b.name;
                });

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to read directory: " + std::string(e.what()));
        }
    }

    void AssetBrowserUI::DrawBreadcrumbs() {
        fs::path currentPath(s_CurrentPath);
        fs::path assetsRoot(GetAssetsRoot());

        currentPath = fs::weakly_canonical(currentPath);
        assetsRoot = fs::weakly_canonical(assetsRoot);

        fs::path relativePath;
        if (currentPath.generic_string().find(assetsRoot.generic_string()) == 0) {
            relativePath = fs::relative(currentPath, assetsRoot);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));

        if (ImGui::SmallButton("Assets")) {
            s_CurrentPath = GetAssetsRoot();
            s_NeedsRefresh = true;
        }

        if (!relativePath.empty() && relativePath.generic_string() != ".") {
            std::vector<fs::path> parts;
            for (const auto& p : relativePath) {
                if (p.generic_string() != "." && !p.empty()) {
                    parts.push_back(p);
                }
            }

            fs::path partialPath = assetsRoot;
            for (size_t i = 0; i < parts.size(); ++i) {
                partialPath /= parts[i];

                ImGui::SameLine(0, 2);
                ImGui::TextDisabled(">");
                ImGui::SameLine(0, 2);

                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton(parts[i].string().c_str())) {
                    s_CurrentPath = partialPath.generic_string();
                    s_NeedsRefresh = true;
                }
                ImGui::PopID();
            }
        }

        ImGui::PopStyleColor(2);
    }

    void AssetBrowserUI::DrawToolbar() {
        ImGui::BeginDisabled(!CanNavigateUp());
        if (ImGui::Button("<")) {
            fs::path current(s_CurrentPath);
            if (current.has_parent_path()) {
                s_CurrentPath = current.parent_path().generic_string();
                s_NeedsRefresh = true;
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        if (ImGui::Button("Refresh")) {
            s_NeedsRefresh = true;
        }
        ImGui::SameLine();

        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Size", &thumbnailSize, 48.0f, 150.0f, "%.0f");
        ImGui::SameLine();

        if (ImGui::Checkbox("Hidden", &showHiddenFiles)) {
            s_NeedsRefresh = true;
        }
    }

    void AssetBrowserUI::DrawAssetGrid() {
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = static_cast<int>(panelWidth / (thumbnailSize + padding));
        if (columnCount < 1) columnCount = 1;

        ImGui::BeginChild("AssetGrid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImGui::Columns(columnCount, nullptr, false);

        for (int i = 0; i < static_cast<int>(s_CurrentEntries.size()); ++i) {
            AssetEntry& entry = s_CurrentEntries[i];

            ImGui::PushID(i);

            bool isSelected = (s_SelectedIndex == i);
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 0.6f));
            }

            ImGui::BeginGroup();

            uint32_t textureID = 0;
            if (entry.type == AssetType::Image) {
                textureID = GetOrLoadThumbnail(entry.path);
            }

            if (textureID != 0) {
                if (ImGui::ImageButton("thumb", (ImTextureID)(uintptr_t)textureID,
                    ImVec2(thumbnailSize, thumbnailSize))) {
                    s_SelectedIndex = i;
                }
            } else {
                const char* icon = GetIconForType(entry.type);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
                if (ImGui::Button(icon, ImVec2(thumbnailSize, thumbnailSize))) {
                    s_SelectedIndex = i;
                }
                ImGui::PopStyleVar();
            }

            // Handle double-click
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                OpenAsset(entry);
            }

            // Context menu
            if (ImGui::BeginPopupContextItem("AssetContextMenu")) {
                DrawAssetContextMenu(entry);
                ImGui::EndPopup();
            }

            // Drag source for sprites
            HandleDragDrop(entry);

            // Label
            std::string displayName = entry.name;
            float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
            if (textWidth > thumbnailSize) {
                while (textWidth > thumbnailSize - 10 && displayName.length() > 3) {
                    displayName.pop_back();
                    textWidth = ImGui::CalcTextSize((displayName + "..").c_str()).x;
                }
                displayName += "..";
            }
            ImGui::TextWrapped("%s", displayName.c_str());

            // Show sprite count badge
            if (entry.type == AssetType::Image && entry.hasMeta && entry.spriteCount > 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "[%d]", entry.spriteCount);
            }

            ImGui::EndGroup();

            if (isSelected) {
                ImGui::PopStyleColor();
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
        ImGui::EndChild();
    }

    void AssetBrowserUI::DrawAssetContextMenu(const AssetEntry& entry) {
        if (entry.type == AssetType::Image) {
            if (ImGui::MenuItem("Open in Sprite Cutter")) {
                TextureCutterUI::OpenTextureCutter(entry.path.string());
            }

            ImGui::Separator();

            if (!entry.hasMeta) {
                if (ImGui::MenuItem("Create Sprite Meta")) {
                    CreateSpriteMetaForImage(entry.path);
                    s_NeedsRefresh = true;
                }
            } else {
                if (ImGui::MenuItem("Edit Sprites")) {
                    TextureCutterUI::OpenTextureCutter(entry.path.string());
                }

                if (ImGui::BeginMenu("Sprites")) {
                    auto sprites = Engine::TextureManager::GetSpritesFromTexture(entry.path);
                    for (const auto& spriteName : sprites) {
                        if (ImGui::MenuItem(spriteName.c_str())) {
                            // Could add functionality here
                        }
                    }
                    if (sprites.empty()) {
                        ImGui::TextDisabled("No sprites");
                    }
                    ImGui::EndMenu();
                }
            }
        }

        if (entry.type == AssetType::Folder) {
            if (ImGui::MenuItem("Open")) {
                s_CurrentPath = entry.path.generic_string();
                s_NeedsRefresh = true;
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Show in Explorer")) {
            #ifdef _WIN32
            std::string cmd = "explorer /select,\"" + entry.path.string() + "\"";
            system(cmd.c_str());
            #endif
        }

        if (ImGui::MenuItem("Copy Path")) {
            ImGui::SetClipboardText(entry.path.string().c_str());
        }
    }

    AssetType AssetBrowserUI::DetermineAssetType(const std::filesystem::path& path) {
        if (fs::is_directory(path)) {
            return AssetType::Folder;
        }

        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
            return AssetType::Image;
        }
        if (ext == ".world") {
            return AssetType::World;
        }
        if (ext == ".prefab") {
            return AssetType::Prefab;
        }
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
            return AssetType::Script;
        }
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
            return AssetType::Audio;
        }
        if (ext == ".ttf" || ext == ".otf") {
            return AssetType::Font;
        }

        return AssetType::Unknown;
    }

    uint32_t AssetBrowserUI::GetOrLoadThumbnail(const std::filesystem::path& imagePath) {
        std::string key = imagePath.string();

        auto it = s_ThumbnailCache.find(key);
        if (it != s_ThumbnailCache.end()) {
            return it->second;
        }

        int width, height, channels;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* data = stbi_load(imagePath.string().c_str(), &width, &height, &channels, 4);

        if (!data) {
            s_ThumbnailCache[key] = 0;
            return 0;
        }

        uint32_t textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(data);

        s_ThumbnailCache[key] = textureID;
        return textureID;
    }

    void AssetBrowserUI::CreateSpriteMetaForImage(const std::filesystem::path& imagePath) {
        Engine::SpriteMetaFile* meta = Engine::TextureManager::GetOrCreateMetaFile(imagePath);
        if (meta) {
            LOG_INFO("Created sprite meta: " + meta->metaFilePath.string());
        } else {
            LOG_ERROR("Failed to create sprite meta for: " + imagePath.string());
        }
    }

    void AssetBrowserUI::OpenAsset(const AssetEntry& entry) {
        switch (entry.type) {
            case AssetType::Folder:
                s_CurrentPath = entry.path.generic_string();
                s_NeedsRefresh = true;
                break;

            case AssetType::Image:
                TextureCutterUI::OpenTextureCutter(entry.path.string());
                break;

            case AssetType::World:
                if (EditorUi::isWorldUnsaved) {
                    s_PendingWorldToLoad = entry.path.stem().string();
                    ImGui::OpenPopup("Save Before Loading?");
                } else {
                    EditorUi::LoadWorld(entry.path.stem().string());
                }
                break;

            default:
                break;
        }
    }

    void AssetBrowserUI::HandleDragDrop(const AssetEntry& entry) {
        if (entry.type != AssetType::Image || !entry.hasMeta) {
            return;
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            auto sprites = Engine::TextureManager::GetSpritesFromTexture(entry.path);
            if (!sprites.empty()) {
                const std::string& spriteName = sprites[0];
                ImGui::SetDragDropPayload("SPRITE_ASSET", spriteName.c_str(), spriteName.size() + 1);

                uint32_t thumbID = GetOrLoadThumbnail(entry.path);
                if (thumbID) {
                    ImGui::Image((ImTextureID)(uintptr_t)thumbID, ImVec2(50, 50));
                }
                ImGui::Text("%s", spriteName.c_str());
            }
            ImGui::EndDragDropSource();
        }
    }

    const char* AssetBrowserUI::GetIconForType(AssetType type) {
        switch (type) {
            case AssetType::Folder:     return "[D]";
            case AssetType::Image:      return "[I]";
            case AssetType::World:      return "[W]";
            case AssetType::Prefab:     return "[P]";
            case AssetType::Script:     return "[C]";
            case AssetType::Audio:      return "[A]";
            case AssetType::Font:       return "[F]";
            case AssetType::SpriteMeta: return "[M]";
            default:                    return "[?]";
        }
    }

} // namespace EditorWindows
