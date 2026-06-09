#include "native_dialog.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <cstring>

namespace Engine::editor {

static constexpr char FILTER[] =
    "Zues World\0*.zworld\0All Files\0*.*\0\0";

std::string dialog_open_file(const char* title) {
    char buf[MAX_PATH] = {};
    OPENFILENAMEA ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFilter     = FILTER;
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = title;
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? buf : std::string{};
}

std::string dialog_save_file(const char* title, const char* default_name) {
    char buf[MAX_PATH] = {};
    if (default_name)
        std::strncpy(buf, default_name, MAX_PATH - 1);
    OPENFILENAMEA ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFilter     = FILTER;
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = title;
    ofn.lpstrDefExt     = "zworld";
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? buf : std::string{};
}

}  // namespace Engine::editor
