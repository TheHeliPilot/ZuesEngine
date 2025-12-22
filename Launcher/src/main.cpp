#include "LauncherUI.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    Zues::LauncherUI launcher;

    if (!launcher.Initialize()) {
        return 1;
    }

    launcher.Run();
    launcher.Shutdown();

    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return main(__argc, __argv);
}
#endif
