#include "launcher.h"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <commdlg.h>
    #include <shlobj.h>
#endif

#include <cstring>

namespace Engine::launcher {

bool pick_open_file(std::string& out_path,
                    const char* title,
                    const char* filter_label,
                    const char* filter_exts) {
#if defined(_WIN32)
    char buf[1024] = "";

    // Filter format: "Label\0pattern\0All Files\0*.*\0\0"
    char filter[256];
    int n = std::snprintf(filter, sizeof(filter), "%s_PLACEHOLDER%s_PLACEHOLDERAll Files_PLACEHOLDER*.*_PLACEHOLDER",
                          filter_label, filter_exts);
    // Replace _PLACEHOLDER with \0 — sscanf-y trick to keep snprintf happy.
    for (int i = 0; i < n; ++i) {
        if (i + 12 <= n && std::strncmp(filter + i, "_PLACEHOLDER", 12) == 0) {
            filter[i] = '\0';
            // Shift the rest left by 11 to remove the rest of the placeholder.
            std::memmove(filter + i + 1, filter + i + 12, n - (i + 12) + 1);
            n -= 11;
        }
    }
    filter[n] = '\0';
    filter[n + 1] = '\0';   // double-null terminator

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
#else
    (void)title; (void)filter_label; (void)filter_exts; (void)out_path;
    return false;   // Linux/macOS: fall back to text-input flow in the UI.
#endif
}

bool pick_folder(std::string& out_path, const char* title) {
#if defined(_WIN32)
    BROWSEINFOA bi = {};
    bi.lpszTitle = title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return false;

    char buf[MAX_PATH] = "";
    const bool ok = SHGetPathFromIDListA(pidl, buf);
    CoTaskMemFree(pidl);
    if (!ok) return false;
    out_path = buf;
    return true;
#else
    (void)title; (void)out_path;
    return false;
#endif
}

}  // namespace Engine::launcher
