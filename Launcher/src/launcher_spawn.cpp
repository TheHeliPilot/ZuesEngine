#include "launcher.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <climits>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace Engine::launcher {

namespace fs = std::filesystem;

namespace {
    fs::path my_dir() {
#if defined(_WIN32)
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return fs::current_path();
        return fs::path(buf).parent_path();
#else
        char buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return fs::current_path();
        buf[n] = '\0';
        return fs::path(buf).parent_path();
#endif
    }
}

bool spawn_editor(const std::string& project_path) {
#if defined(_WIN32)
    fs::path editor = my_dir() / "editor.exe";
#else
    fs::path editor = my_dir() / "editor";
#endif
    std::error_code ec;
    if (!fs::exists(editor, ec)) {
        // Fallback: editor2.exe — used during dev when the canonical
        // editor.exe is held open by Windows (defender hand-off, browser
        // download stub, etc.) and the build had to land somewhere else.
#if defined(_WIN32)
        editor = my_dir() / "editor2.exe";
#else
        editor = my_dir() / "editor2";
#endif
        if (!fs::exists(editor, ec)) return false;
    }

#if defined(_WIN32)
    // Quote the editor path and the --project arg so spaces survive.
    std::string cmd = "\"" + editor.string() + "\" \"--project=" + project_path + "\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessA(
        /* lpApplicationName    */ nullptr,
        /* lpCommandLine        */ cmd.data(),
        /* lpProcessAttributes  */ nullptr,
        /* lpThreadAttributes   */ nullptr,
        /* bInheritHandles      */ FALSE,
        /* dwCreationFlags      */ DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
        /* lpEnvironment        */ nullptr,
        /* lpCurrentDirectory   */ nullptr,
        /* lpStartupInfo        */ &si,
        /* lpProcessInformation */ &pi);

    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        const std::string arg = "--project=" + project_path;
        execl(editor.c_str(), editor.c_str(), arg.c_str(), nullptr);
        std::_Exit(127);
    }
    return true;
#endif
}

bool run_build_sync(const fs::path& project_dir, std::string& out_output) {
#if defined(_WIN32)
    // Build command: cmd.exe /C build.bat, working dir = project_dir
    fs::path bat = project_dir / "build.bat";
    std::string cmd = "cmd.exe /C \"\"" + bat.string() + "\"\"";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength              = sizeof(sa);
    sa.bInheritHandle       = TRUE;

    // Pipe: child writes stdout+stderr → parent reads
    HANDLE pipe_r = nullptr, pipe_w = nullptr;
    if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) return false;
    SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdOutput  = pipe_w;
    si.hStdError   = pipe_w;
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr, cmd.data(), nullptr, nullptr,
        /*bInheritHandles*/ TRUE,
        CREATE_NO_WINDOW, nullptr,
        project_dir.string().c_str(),
        &si, &pi);

    CloseHandle(pipe_w);  // parent closes write end so ReadFile sees EOF
    if (!ok) { CloseHandle(pipe_r); return false; }

    out_output.clear();
    char buf[1024];
    DWORD bytes_read = 0;
    while (ReadFile(pipe_r, buf, sizeof(buf), &bytes_read, nullptr) && bytes_read > 0)
        out_output.append(buf, bytes_read);

    CloseHandle(pipe_r);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exit_code == 0;
#else
    (void)project_dir;
    out_output = "run_build_sync: not implemented on this platform";
    return false;
#endif
}

}  // namespace Engine::launcher
