#ifndef EDITORWINDOWS_ASSETBROWSERUI_H
#define EDITORWINDOWS_ASSETBROWSERUI_H

#include <string>
#include <filesystem>
#include <vector>
#include <unordered_map>
#include "EditorUi.h"

namespace EditorWindows {

    // Icon font codepoints from IconLibs.ttf (Private Use Area 0xE000-0xF8FF)
    // These are common icons - you may need to adjust based on your specific icon font
    namespace Icons {
        // Using common icon codepoints - adjust these based on your IconLibs.ttf content
        constexpr const char* Folder      = "\xEE\x80\x80";  // 0xE000
        constexpr const char* FolderOpen  = "\xEE\x80\x81";  // 0xE001
        constexpr const char* File        = "\xEE\x80\x82";  // 0xE002
        constexpr const char* Image       = "\xEE\x80\x83";  // 0xE003
        constexpr const char* Code        = "\xEE\x80\x84";  // 0xE004
        constexpr const char* Audio       = "\xEE\x80\x85";  // 0xE005
        constexpr const char* Font        = "\xEE\x80\x86";  // 0xE006
        constexpr const char* Scene       = "\xEE\x80\x87";  // 0xE007
        constexpr const char* Prefab      = "\xEE\x80\x88";  // 0xE008
        constexpr const char* Refresh     = "\xEE\x80\x89";  // 0xE009
        constexpr const char* Back        = "\xEE\x80\x8A";  // 0xE00A
        constexpr const char* Home        = "\xEE\x80\x8B";  // 0xE00B
    }

    enum class AssetType {
        Unknown,
        Folder,
        Image,      // .png, .jpg, .jpeg, .bmp, .tga
        World,      // .world (world files)
        Prefab,     // .prefab
        Script,     // .cpp, .h
        Audio,      // .wav, .mp3, .ogg
        Font,       // .ttf, .otf
        SpriteMeta  // .sprite.meta
    };

    struct AssetEntry {
        std::filesystem::path path;
        std::string name;
        AssetType type = AssetType::Unknown;
        uint32_t thumbnailID = 0;       // OpenGL texture ID for preview
        bool hasMeta = false;           // Does this asset have a .sprite.meta file?
        int spriteCount = 0;            // Number of sprites in meta file
    };

    /**
    * @brief ImGui window for browsing and managing engine assets.
    */
    class AssetBrowserUI final {
    public:
        static void AssetBrowserWindow();

        // Settings
        static float thumbnailSize;
        static float padding;
        static bool showHiddenFiles;
        static float folderTreeWidth;

    private:
        static std::string s_CurrentPath;
        static std::string s_AssetsRoot;  // Root Assets folder - cannot navigate above this
        static std::vector<AssetEntry> s_CurrentEntries;
        static std::unordered_map<std::string, uint32_t> s_ThumbnailCache;
        static int s_SelectedIndex;
        static bool s_NeedsRefresh;

        static void RefreshDirectory();
        static void DrawBreadcrumbs();
        static void DrawToolbar();
        static void DrawFolderTree();
        static void DrawFolderTreeNode(const std::filesystem::path& folderPath);
        static void DrawAssetGrid();
        static void DrawAssetContextMenu(const AssetEntry& entry);

        static AssetType DetermineAssetType(const std::filesystem::path& path);
        static uint32_t GetOrLoadThumbnail(const std::filesystem::path& imagePath);
        static void CreateSpriteMetaForImage(const std::filesystem::path& imagePath);
        static void OpenAsset(const AssetEntry& entry);
        static void HandleDragDrop(const AssetEntry& entry);

        // Navigation helpers
        static bool CanNavigateUp();
        static std::string GetAssetsRoot();

        // Icon helpers
        static const char* GetIconForType(AssetType type);
    };
}

#endif //EDITORWINDOWS_ASSETBROWSERUI_H
