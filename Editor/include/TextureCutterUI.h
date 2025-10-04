#pragma once
#include <string>

namespace EditorWindows {
    class TextureCutterUI final {
    public:
        static bool isOpen;

        static void OpenTextureCutter(const std::string &texturePath);

        // Logger Window function
        static void TextureCutterWindow();
    };
}
