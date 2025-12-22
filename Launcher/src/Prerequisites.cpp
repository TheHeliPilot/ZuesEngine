#include "Prerequisites.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

#include <array>
#include <cstdio>
#include <memory>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;

namespace Zues {

PrerequisiteCheckResult Prerequisites::CheckAll() {
    PrerequisiteCheckResult result;

    result.prerequisites.push_back(CheckCMake());
    result.prerequisites.push_back(CheckMSVC());

    result.allMet = true;
    for (const auto& prereq : result.prerequisites) {
        if (!prereq.installed || !prereq.versionOk) {
            result.allMet = false;
            break;
        }
    }

    return result;
}

PrerequisiteInfo Prerequisites::CheckCMake() {
    PrerequisiteInfo info;
    info.name = "CMake";
    info.requiredVersion = "3.20";
    info.downloadUrl = "https://cmake.org/download/";
    info.installInstructions = "Download and install CMake from cmake.org.\nMake sure to add CMake to your PATH during installation.";

    auto cmakePath = FindExe("cmake");
    if (cmakePath) {
        info.path = *cmakePath;
        info.installed = true;

        auto version = GetVersion(*cmakePath, "--version");
        if (version) {
            // Parse version from "cmake version X.Y.Z"
            std::regex versionRegex(R"(cmake version (\d+\.\d+(?:\.\d+)?))");
            std::smatch match;
            if (std::regex_search(*version, match, versionRegex)) {
                info.version = match[1].str();
                info.versionOk = CompareVersions(info.version, info.requiredVersion);
            }
        }
    }

    return info;
}

PrerequisiteInfo Prerequisites::CheckMSVC() {
    PrerequisiteInfo info;
    info.name = "MSVC (Visual Studio Build Tools)";
    info.requiredVersion = "19.0"; // VS 2015+
    info.downloadUrl = "https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022";
    info.installInstructions = "Download Visual Studio Build Tools and install the 'Desktop development with C++' workload.";

#ifdef _WIN32
    // Check for cl.exe in common Visual Studio paths
    std::vector<std::string> vsVersions = {"2022", "2019", "2017"};
    std::vector<std::string> editions = {"Enterprise", "Professional", "Community", "BuildTools"};

    for (const auto& vsVer : vsVersions) {
        for (const auto& edition : editions) {
            std::string basePath = "C:/Program Files/Microsoft Visual Studio/" + vsVer + "/" + edition +
                                   "/VC/Tools/MSVC";
            if (fs::exists(basePath)) {
                // Find the latest version directory
                for (const auto& entry : fs::directory_iterator(basePath)) {
                    if (entry.is_directory()) {
                        std::string clPath = entry.path().string() + "/bin/Hostx64/x64/cl.exe";
                        if (fs::exists(clPath)) {
                            info.path = clPath;
                            info.installed = true;
                            info.version = entry.path().filename().string();
                            info.versionOk = true; // If we found a modern VS, it's good enough
                            return info;
                        }
                    }
                }
            }
        }
    }

    // Also check if cl.exe is in PATH (developer command prompt)
    auto clPath = FindExe("cl");
    if (clPath) {
        info.path = *clPath;
        info.installed = true;
        info.versionOk = true;
        info.version = "Found in PATH";
    }
#endif

    return info;
}

std::optional<std::string> Prerequisites::FindExe(const std::string& name) {
#ifdef _WIN32
    std::string exeName = name;
    if (exeName.find(".exe") == std::string::npos) {
        exeName += ".exe";
    }

    // Check PATH
    char pathBuffer[MAX_PATH];
    DWORD result = SearchPathA(nullptr, exeName.c_str(), nullptr, MAX_PATH, pathBuffer, nullptr);
    if (result > 0) {
        return std::string(pathBuffer);
    }

    // Check common paths
    for (const auto& commonPath : GetCommonPaths()) {
        std::string fullPath = commonPath + "/" + exeName;
        if (fs::exists(fullPath)) {
            return fullPath;
        }
    }
#endif

    return std::nullopt;
}

std::optional<std::string> Prerequisites::GetVersion(const std::string& exePath, const std::string& versionArg) {
#ifdef _WIN32
    std::string command = "\"" + exePath + "\" " + versionArg + " 2>&1";

    std::array<char, 256> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
    if (!pipe) {
        return std::nullopt;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    return result;
#else
    return std::nullopt;
#endif
}

void Prerequisites::OpenURL(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void Prerequisites::OpenFolder(const std::string& path) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "explore", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

std::vector<std::string> Prerequisites::GetCommonPaths() {
    std::vector<std::string> paths;

#ifdef _WIN32
    paths.push_back("C:/Program Files/CMake/bin");
    paths.push_back("C:/Program Files (x86)/CMake/bin");

    // Add user's local app data
    const char* appdata = std::getenv("LOCALAPPDATA");
    if (appdata) {
        paths.push_back(std::string(appdata) + "/CMake/bin");
    }
#endif

    return paths;
}

bool Prerequisites::CompareVersions(const std::string& current, const std::string& required) {
    auto parseVersion = [](const std::string& ver) -> std::vector<int> {
        std::vector<int> parts;
        std::istringstream iss(ver);
        std::string part;
        while (std::getline(iss, part, '.')) {
            try {
                parts.push_back(std::stoi(part));
            } catch (...) {
                parts.push_back(0);
            }
        }
        while (parts.size() < 3) parts.push_back(0);
        return parts;
    };

    auto cur = parseVersion(current);
    auto req = parseVersion(required);

    for (size_t i = 0; i < 3; ++i) {
        if (cur[i] > req[i]) return true;
        if (cur[i] < req[i]) return false;
    }
    return true; // Equal
}

} // namespace Zues
