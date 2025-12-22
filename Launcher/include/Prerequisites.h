#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace Zues {

struct PrerequisiteInfo {
    std::string name;
    std::string version;
    std::string path;
    bool installed = false;
    bool versionOk = false;
    std::string requiredVersion;
    std::string downloadUrl;
    std::string installInstructions;
};

struct PrerequisiteCheckResult {
    bool allMet = false;
    std::vector<PrerequisiteInfo> prerequisites;
};

class Prerequisites {
public:
    // Check all required prerequisites
    static PrerequisiteCheckResult CheckAll();

    // Individual checks
    static PrerequisiteInfo CheckCMake();
    static PrerequisiteInfo CheckMSVC();

    // Attempt to find executable in PATH and common locations
    // Note: Named FindExe to avoid conflict with Windows FindExecutable macro
    static std::optional<std::string> FindExe(const std::string& name);

    // Get version from executable
    static std::optional<std::string> GetVersion(const std::string& exePath, const std::string& versionArg = "--version");

    // Open URL in default browser
    static void OpenURL(const std::string& url);

    // Open folder in explorer
    static void OpenFolder(const std::string& path);

private:
    static std::vector<std::string> GetCommonPaths();
    static bool CompareVersions(const std::string& current, const std::string& required);
};

} // namespace Zues
