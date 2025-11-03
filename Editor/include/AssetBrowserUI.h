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
    class AssetBrowserUI final {
    public:
        // Static function to render the Asset Browser window
        static void AssetBrowserWindow();

    private:


    };
}

#endif //EDITORWINDOWS_ASSETBROWSERUI_H
