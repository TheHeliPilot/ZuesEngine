#ifndef EDITORWINDOWS_ASSETBROWSERUI_H
#define EDITORWINDOWS_ASSETBROWSERUI_H

#include <string>
#include <filesystem> // Correctly include the standard filesystem header
#include <vector>
#include "EditorUi.h" // Include EditorUi.h to get access to EditorUi::projectDir

namespace EditorWindows {

    /**
    * @brief ImGui window for browsing and managing engine assets.
    */
    class AssetBrowserUI final { // Added 'final' as requested
    public:
        // Static function to render the Asset Browser window
        static void AssetBrowserWindow();

        // Placeholder callback function that is fired when a file is double-clicked
        // Kept static to match the call in AssetBrowserWindow() implementation.
        static void HandleFileDoubleClick(const std::filesystem::path& filePath);

    private:
        // --- State ---
        // The root path of the project assets. (Set to a temporary default for safe initialization)
        inline static const std::filesystem::path s_ProjectRootPath = ".";

        // The currently selected directory shown in the right pane.
        inline static std::filesystem::path m_CurrentDirectory = s_ProjectRootPath;

        // The currently selected file path in the right pane (for single-click selection)
        inline static std::filesystem::path m_SelectedFilePath;
    };
}

#endif //EDITORWINDOWS_ASSETBROWSERUI_H
