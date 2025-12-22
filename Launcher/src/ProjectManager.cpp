#include "ProjectManager.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace fs = std::filesystem;

namespace Zues {

LauncherProjectManager::LauncherProjectManager() {
    m_ConfigPath = GetConfigDir() / "projects.txt";
    LoadProjects();
}

void LauncherProjectManager::LoadProjects() {
    m_Projects.clear();

    if (!fs::exists(m_ConfigPath)) {
        return;
    }

    std::ifstream file(m_ConfigPath);
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Each line is a project path
        auto projectInfo = ValidateProject(line);
        if (projectInfo) {
            m_Projects.push_back(*projectInfo);
        }
    }
}

void LauncherProjectManager::SaveProjects() {
    // Ensure config directory exists
    fs::create_directories(m_ConfigPath.parent_path());

    std::ofstream file(m_ConfigPath);
    for (const auto& project : m_Projects) {
        file << project.path << "\n";
    }
}

bool LauncherProjectManager::AddProject(const std::string& projectPath) {
    // Validate the project
    auto projectInfo = ValidateProject(projectPath);
    if (!projectInfo) {
        return false;
    }

    // Check if already in list
    for (const auto& existing : m_Projects) {
        if (existing.path == projectPath) {
            return true; // Already exists
        }
    }

    m_Projects.push_back(*projectInfo);
    SaveProjects();
    return true;
}

void LauncherProjectManager::RemoveProject(const std::string& projectPath) {
    m_Projects.erase(
        std::remove_if(m_Projects.begin(), m_Projects.end(),
                       [&projectPath](const ProjectInfo& p) { return p.path == projectPath; }),
        m_Projects.end());
    SaveProjects();
}

bool LauncherProjectManager::OpenProject(const ProjectInfo& project) {
    std::string editorPath = GetEditorPath();

    if (!fs::exists(editorPath)) {
        return false;
    }

#ifdef _WIN32
    // Launch editor with project path as argument
    std::string command = "\"" + editorPath + "\" \"" + project.path + "\"";

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    if (CreateProcessA(
            nullptr,
            const_cast<char*>(command.c_str()),
            nullptr,
            nullptr,
            FALSE,
            DETACHED_PROCESS,
            nullptr,
            nullptr,
            &si,
            &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
#endif

    return false;
}

bool LauncherProjectManager::CreateProject(const std::string& name, const std::string& location) {
    fs::path projectDir = fs::path(location) / name;
    fs::path launcherDir = GetLauncherDirectory();
    fs::path templatesDir = launcherDir / "Templates";

    // Check if templates exist
    if (!fs::exists(templatesDir / "GameProject.cpp.template")) {
        // Templates not found
        return false;
    }

    try {
        // 1. Create project directories
        fs::create_directories(projectDir);
        fs::create_directories(projectDir / "Assets");
        fs::create_directories(projectDir / "Worlds");
        fs::create_directories(projectDir / "Source");
        fs::create_directories(projectDir / "Builds");

        // 2. Create project.zues config file
        {
            std::ofstream configFile(projectDir / "project.zues");
            configFile << "Name=" << name << "\n";
            configFile.close();
        }

        // Helper to replace placeholders in template content
        auto replacePlaceholder = [](std::string& content, const std::string& placeholder, const std::string& value) {
            size_t pos = 0;
            while ((pos = content.find(placeholder, pos)) != std::string::npos) {
                content.replace(pos, placeholder.length(), value);
                pos += value.length();
            }
        };

        // Helper to convert backslashes to forward slashes for CMake
        auto toForwardSlashes = [](std::string s) {
            for (char& c : s) { if (c == '\\') c = '/'; }
            return s;
        };

        // 3. Copy and process GameProject.cpp.template
        {
            std::ifstream templateFile(templatesDir / "GameProject.cpp.template");
            std::stringstream buffer;
            buffer << templateFile.rdbuf();
            std::string content = buffer.str();
            templateFile.close();

            replacePlaceholder(content, "{{PROJECT_NAME}}", name);

            std::ofstream outFile(projectDir / "Source" / (name + ".cpp"));
            outFile << content;
            outFile.close();
        }

        // 4. Copy and process CMakeLists.txt.template
        {
            std::ifstream templateFile(templatesDir / "CMakeLists.txt.template");
            std::stringstream buffer;
            buffer << templateFile.rdbuf();
            std::string content = buffer.str();
            templateFile.close();

            // Calculate paths relative to launcher directory
            fs::path includeDir = launcherDir / "include";
            fs::path libDir = launcherDir / "lib";
            fs::path zuesEngineLib = launcherDir / "libZuesEngine.dll";

            replacePlaceholder(content, "{{PROJECT_NAME}}", name);
            replacePlaceholder(content, "{{ENGINE_INCLUDE_DIR}}", toForwardSlashes(includeDir.string()));
            replacePlaceholder(content, "{{GLFW_INCLUDE_DIR}}", toForwardSlashes((includeDir / "GLFW").string()));
            replacePlaceholder(content, "{{GLAD_INCLUDE_DIR}}", toForwardSlashes((includeDir / "glad").string()));
            replacePlaceholder(content, "{{ZUES_ENGINE_LIB}}", toForwardSlashes(zuesEngineLib.string()));

            std::ofstream outFile(projectDir / "Source" / "CMakeLists.txt");
            outFile << content;
            outFile.close();
        }

        // 5. Create root CMakeLists.txt for IDE integration
        {
            std::ofstream outFile(projectDir / "CMakeLists.txt");
            outFile << "# Top-level CMakeLists.txt for CLion/IDE integration\n";
            outFile << "cmake_minimum_required(VERSION 3.10)\n";
            outFile << "project(" << name << ")\n\n";
            outFile << "# Define a default build directory for CLion if Presets are ignored\n";
            outFile << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Builds)\n\n";
            outFile << "# Include the actual project source definition\n";
            outFile << "add_subdirectory(Source)\n";
            outFile.close();
        }

        // 6. Create CMakePresets.json
        {
            fs::path includeDir = launcherDir / "include";
            fs::path libDir = launcherDir / "lib";

            std::ofstream outFile(projectDir / "CMakePresets.json");
            outFile << "{\n";
            outFile << "  \"version\": 3,\n";
            outFile << "  \"configurePresets\": [\n";
            outFile << "    {\n";
            outFile << "      \"name\": \"dev-config\",\n";
            outFile << "      \"displayName\": \"Engine-Configured Development Build\",\n";
            outFile << "      \"generator\": \"Ninja\",\n";
            outFile << "      \"binaryDir\": \"${sourceDir}/Builds/cmake\",\n";
            outFile << "      \"cacheVariables\": {\n";
            outFile << "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
            outFile << "        \"ENGINE_INCLUDE_DIR\": \"" << toForwardSlashes(includeDir.string()) << "\",\n";
            outFile << "        \"GLFW_INCLUDE_DIR\": \"" << toForwardSlashes((includeDir / "GLFW").string()) << "\",\n";
            outFile << "        \"GLAD_INCLUDE_DIR\": \"" << toForwardSlashes((includeDir / "glad").string()) << "\"\n";
            outFile << "      }\n";
            outFile << "    }\n";
            outFile << "  ]\n";
            outFile << "}\n";
            outFile.close();
        }

        // Add project to launcher's list
        AddProject(projectDir.string());
        return true;

    } catch (const std::exception& e) {
        // Clean up on failure
        try {
            if (fs::exists(projectDir)) {
                fs::remove_all(projectDir);
            }
        } catch (...) {}
        return false;
    }
}

std::optional<ProjectInfo> LauncherProjectManager::ValidateProject(const std::string& path) {
    fs::path projectPath(path);
    fs::path projectFile = projectPath / "project.zues";

    if (!fs::exists(projectFile)) {
        // Maybe they pointed to the file directly
        if (projectPath.filename() == "project.zues" && fs::exists(projectPath)) {
            projectFile = projectPath;
            projectPath = projectPath.parent_path();
        } else {
            return std::nullopt;
        }
    }

    ProjectInfo info;
    info.path = projectPath.string();
    info.valid = true;

    // Parse project.zues to get name and version
    std::ifstream file(projectFile);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Simple JSON parsing for name
    auto findValue = [&content](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = content.find(searchKey);
        if (pos == std::string::npos) return "";

        pos += searchKey.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '"')) pos++;

        size_t end = content.find('"', pos);
        if (end == std::string::npos) return "";

        return content.substr(pos, end - pos);
    };

    info.name = findValue("name");
    info.version = findValue("version");

    if (info.name.empty()) {
        info.name = projectPath.filename().string();
    }

    // Get last modified time as "last opened"
    auto ftime = fs::last_write_time(projectFile);
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    auto time = std::chrono::system_clock::to_time_t(sctp);

    std::tm tm;
    localtime_s(&tm, &time);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    info.lastOpened = oss.str();

    return info;
}

fs::path LauncherProjectManager::GetConfigDir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return fs::path(appdata) / "ZuesEngine";
    }
    return fs::current_path() / ".zues";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / ".config" / "ZuesEngine";
    }
    return fs::current_path() / ".zues";
#endif
}

std::string LauncherProjectManager::GetEditorPath() const {
    return (GetLauncherDirectory() / "ZuesEditor.exe").string();
}

fs::path LauncherProjectManager::GetLauncherDirectory() {
#ifdef _WIN32
    char modulePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH)) {
        return fs::path(modulePath).parent_path();
    }
#endif
    return fs::current_path();
}

} // namespace Zues
