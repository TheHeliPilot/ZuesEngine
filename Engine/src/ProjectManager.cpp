#include "../include/Engine/ProjectManager.h"
#include "../include/Engine/EngineDefines.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib> // Required for std::system
#include <vector>
#include <filesystem>
#include <string>
#include <thread>
#include <atomic>
#include <mutex> // Necessary since std::mutex is now used in the cpp

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../include/Engine/Core.h"

namespace Engine {

    // Helper function to get the directory where the executable is located
    std::filesystem::path GetExecutableDirectory() {
#ifdef _WIN32
        char modulePath[MAX_PATH];
        if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH)) {
            return std::filesystem::path(modulePath).parent_path();
        }
#endif
        // Fallback to current path if platform-specific method fails
        return std::filesystem::current_path();
    }

    // Helper function to get the ZuesEngine root directory
    // This works whether running from bin/ or bin/Debug/ etc.
    std::filesystem::path GetZuesEngineRoot() {
        std::filesystem::path exeDir = GetExecutableDirectory();

        // The executable could be in:
        // - ZuesEngine/bin/ZuesEditor.exe (1 level deep)
        // - ZuesEngine/bin/Debug/ZuesEditor.exe (2 levels deep)
        // - ZuesEngine/cmake-build-debug/Editor/ZuesEditor.exe (2+ levels deep)

        // Walk up the directory tree looking for the Engine folder
        std::filesystem::path current = exeDir;
        for (int i = 0; i < 5; ++i) { // Check up to 5 levels
            if (std::filesystem::exists(current / "Engine" / "include" / "Engine") &&
                std::filesystem::exists(current / "Editor" / "Templates")) {
                return current;
            }
            if (current.has_parent_path() && current.parent_path() != current) {
                current = current.parent_path();
            } else {
                break;
            }
        }

        // Fallback: assume we're 2 levels deep from current_path (old behavior)
        LOG_WARN("Could not find ZuesEngine root from executable path, using fallback");
        return std::filesystem::current_path().parent_path().parent_path();
    }
    // Initialize static members (All declared in the fixed header)
    Project* ProjectManager::s_CurrentProject = nullptr;
    const std::string ProjectManager::CONFIG_FILE_NAME = "project.zues";
    const std::string ProjectManager::ENGINE_INCLUDE_KEY = "EngineIncludePath=";

    std::atomic<bool> ProjectManager::s_IsBuilding = false;
    std::mutex ProjectManager::s_BuildMutex;

    // CRITICAL FIX 4: Initialization of the static member s_BuildParams
    BuildParams ProjectManager::s_BuildParams;


    // --- Helper 1: Dynamically calculate Engine Include Path ---
    std::filesystem::path CalculateEngineIncludePath() {
        try {
            const std::filesystem::path zuesEngineRoot = GetZuesEngineRoot();
            const std::filesystem::path engineIncludePath = zuesEngineRoot / "Engine/include/Engine";

            if (!std::filesystem::exists(engineIncludePath)) {
                LOG_WARN("Warning: Calculated Engine Include Path does not exist: " + engineIncludePath.string());
            }
            return std::filesystem::absolute(engineIncludePath);

        } catch (const std::exception& e) {
            LOG_ERROR("Critical: Failed to calculate engine path: " + std::string(e.what()));
            return {};
        }
    }
    // -------------------------------------------------------------

    // --- Helper 2: Overwrite/Write Engine Path to Config ---
    void WriteEnginePathToConfig(const std::filesystem::path& configPath, const std::filesystem::path& newPath) {
        std::vector<std::string> lines;
        bool keyFound = false;
        std::ifstream inFile(configPath);
        std::string line;
        while (std::getline(inFile, line)) {
            // FIX 5: Use ProjectManager::ENGINE_INCLUDE_KEY for scope resolution
            if (line.rfind(ProjectManager::ENGINE_INCLUDE_KEY, 0) == 0) {
                lines.push_back(ProjectManager::ENGINE_INCLUDE_KEY + newPath.string());
                keyFound = true;
            } else {
                lines.push_back(line);
            }
        }
        inFile.close();

        if (!keyFound) {
            // FIX 6: Use ProjectManager::ENGINE_INCLUDE_KEY
            lines.push_back(ProjectManager::ENGINE_INCLUDE_KEY + newPath.string());
        }

        std::ofstream outFile(configPath, std::ios::trunc);
        for (const auto& l : lines) {
            outFile << l << "\n";
        }
        outFile.close();
        LOG_INFO("Updated EngineIncludePath in config: " + newPath.string());
    }
    // ---------------------------------------------------------

    // --- Core Project Management Logic --- (UNCHANGED)

    bool ProjectManager::OpenOrCreate(const std::filesystem::path& projectPath) {
        if (s_CurrentProject) {
            LOG_WARN("Closing existing project before opening a new one.");
            delete s_CurrentProject;
            s_CurrentProject = nullptr;
        }

        if (!std::filesystem::exists(projectPath) || !std::filesystem::exists(projectPath / CONFIG_FILE_NAME)) {
            return CreateNewProject(projectPath);
        } else {
            return LoadExistingProject(projectPath);
        }
    }

    bool ProjectManager::LoadExistingProject(const std::filesystem::path& projectPath) {
        const std::string projectName = projectPath.filename().string();
        const std::filesystem::path configPath = projectPath / CONFIG_FILE_NAME;

        const std::filesystem::path currentEngineIncludePath = CalculateEngineIncludePath();
        if (currentEngineIncludePath.empty()) return false;

        WriteEnginePathToConfig(configPath, currentEngineIncludePath);

        s_CurrentProject = new Project{
            .Name = projectName,
            .RootPath = projectPath,
            .ConfigFilePath = configPath,
            .EngineIncludePath = currentEngineIncludePath
        };

        LOG_INFO("Project loaded successfully: " + projectName);
        if (!Core::LoadWorld(projectPath.string() + "/Worlds/World.world")) {
            LOG_WARN("Failed to load world at " + projectPath.string() + "/Worlds/World.world. Saving default.");
            if (!Core::SaveWorld(projectPath.string() + "/Worlds/")) {
                LOG_ERROR("Failed to save world!");
            }
        }else LOG_INFO("Loaded world " + projectPath.string() + "/Worlds/World.world");
        return true;
    }

    // Helper function to find the Templates directory
    // Checks: 1) exe_dir/Templates (deployed), 2) ZuesEngineRoot/Editor/Templates (dev)
    std::filesystem::path GetTemplatesDirectory() {
        std::filesystem::path exeDir = GetExecutableDirectory();

        // First check if Templates folder exists in same directory as executable (deployed build)
        std::filesystem::path deployedTemplates = exeDir / "Templates";
        if (std::filesystem::exists(deployedTemplates / "GameProject.cpp.template")) {
            return deployedTemplates;
        }

        // Otherwise, look for Editor/Templates in the engine root (development build)
        std::filesystem::path zuesRoot = GetZuesEngineRoot();
        std::filesystem::path devTemplates = zuesRoot / "Editor" / "Templates";
        if (std::filesystem::exists(devTemplates / "GameProject.cpp.template")) {
            return devTemplates;
        }

        // Fallback to dev path even if not found (will error later)
        LOG_WARN("Could not find Templates directory, using fallback");
        return devTemplates;
    }

    bool ProjectManager::CreateNewProject(const std::filesystem::path& projectPath) {
        try {
            // --- 0. Path Setup & Calculation ---
            std::string projectName = projectPath.filename().string();
            std::filesystem::path sourcePath = projectPath / "Source";

            std::filesystem::path zuesEngineRoot = GetZuesEngineRoot();
            std::filesystem::path engineRoot = zuesEngineRoot / "Engine";
            std::filesystem::path templatesDir = GetTemplatesDirectory();

            LOG_INFO("ZuesEngine root: " + zuesEngineRoot.string());
            LOG_INFO("Templates directory: " + templatesDir.string());

            // Calculate all paths needed for the project and CMakePresets.json
            std::filesystem::path currentEngineIncludePath = CalculateEngineIncludePath();
            if (currentEngineIncludePath.empty()) return false;

            std::string glfwIncludePath = (engineRoot / "extern/glfw/include").string();
            std::string gladIncludePath = (engineRoot / "extern/glad/include").string();
            std::string engineIncludePath = currentEngineIncludePath.string();

            // Combine library paths (same logic as in BuildProject)
            std::string engineLibPaths = (
                (engineRoot / "cmake-build-debug/libEngine.a").string() + ";" +
                (engineRoot / "extern/glfw/cmake-build-debug/src/libglfw3.a").string() + ";" +
                (engineRoot / "extern/glad/cmake-build-debug/libglad.a").string() + ";" +
                (engineRoot / "extern/enet/cmake-build-debug/libenet.a").string()
            );

            // Replace Windows backslashes with forward slashes for CMake/JSON compatibility
            auto replace_slashes = [](std::string& s) {
                for (char& c : s) { if (c == '\\') c = '/'; }
            };

            replace_slashes(engineIncludePath);
            replace_slashes(glfwIncludePath);
            replace_slashes(gladIncludePath);
            replace_slashes(engineLibPaths);

            // 1. Create Directories (UNCHANGED)
            if (!std::filesystem::create_directories(projectPath) && !std::filesystem::exists(projectPath)) {
                LOG_ERROR("Failed to create project root directory: " + projectPath.string());
                return false;
            }

            std::filesystem::create_directory(projectPath / "Assets");
            std::filesystem::create_directory(projectPath / "Worlds");
            std::filesystem::create_directory(sourcePath);
            std::filesystem::create_directory(projectPath / "Builds");


            // 3. Create config file (UNCHANGED)
            std::filesystem::path configPath = projectPath / CONFIG_FILE_NAME;
            std::ofstream configFile(configPath);
            configFile << "Name=" << projectName << "\n";
            // FIX 7: Use ProjectManager::ENGINE_INCLUDE_KEY
            configFile << ProjectManager::ENGINE_INCLUDE_KEY << currentEngineIncludePath.string() << "\n";
            configFile.close();

            // 4. Create main C++ source file
            std::filesystem::path mainCppPath = sourcePath / (projectName + ".cpp");
            std::filesystem::path cppTemplatePath = templatesDir / "GameProject.cpp.template";

            if (!std::filesystem::exists(cppTemplatePath)) {
                LOG_ERROR("FATAL: C++ template file not found. Ensure it exists at: " + cppTemplatePath.string());
                return false;
            }

            std::ifstream templateFile(cppTemplatePath);
            std::stringstream buffer;
            buffer << templateFile.rdbuf();
            std::string mainTemplate = buffer.str();
            templateFile.close();

            const std::string placeholder = "{{PROJECT_NAME}}";
            size_t pos = 0;
            while ((pos = mainTemplate.find(placeholder, pos)) != std::string::npos) {
                mainTemplate.replace(pos, placeholder.length(), projectName);
                pos += projectName.length();
            }

            std::ofstream mainCppFile(mainCppPath);
            mainCppFile << mainTemplate;
            mainCppFile.close();

            // 5. Create Project CMakeLists.txt using template (Source/CMakeLists.txt)
            std::filesystem::path cmakePath = sourcePath / "CMakeLists.txt";
            std::filesystem::path cmakeTemplatePath = templatesDir / "CMakeLists.txt.template";

            if (!std::filesystem::exists(cmakeTemplatePath)) {
                LOG_ERROR("FATAL: CMakeLists.txt template file not found. Ensure it exists at: " + cmakeTemplatePath.string());
                return false;
            }

            std::ifstream cmakeTemplateFile(cmakeTemplatePath);
            std::stringstream cmakeBuffer;
            cmakeBuffer << cmakeTemplateFile.rdbuf();
            std::string cmakeTemplate = cmakeBuffer.str();
            cmakeTemplateFile.close();

            // Find the ZuesEngine library directory (contains import libraries)
            std::filesystem::path zuesEngineLibDir = zuesEngineRoot / "bin" / "ZuesEngine" / "lib";
            std::string zuesEngineLibDirStr = zuesEngineLibDir.string();
            replace_slashes(zuesEngineLibDirStr);

            // Replace all placeholders in CMakeLists.txt template
            auto replacePlaceholder = [](std::string& content, const std::string& placeholder, const std::string& value) {
                size_t pos = 0;
                while ((pos = content.find(placeholder, pos)) != std::string::npos) {
                    content.replace(pos, placeholder.length(), value);
                    pos += value.length();
                }
            };

            replacePlaceholder(cmakeTemplate, "{{PROJECT_NAME}}", projectName);
            replacePlaceholder(cmakeTemplate, "{{ENGINE_INCLUDE_DIR}}", engineIncludePath);
            replacePlaceholder(cmakeTemplate, "{{GLFW_INCLUDE_DIR}}", glfwIncludePath);
            replacePlaceholder(cmakeTemplate, "{{GLAD_INCLUDE_DIR}}", gladIncludePath);
            replacePlaceholder(cmakeTemplate, "{{ZUES_ENGINE_LIB_DIR}}", zuesEngineLibDirStr);

            std::ofstream cmakeFile(cmakePath);
            cmakeFile << cmakeTemplate;
            cmakeFile.close();

            // 5.5. Create Top-Level CMakeLists.txt (NEW FOR CLION)
            std::filesystem::path rootCMakePath = projectPath / "CMakeLists.txt";
            std::ofstream rootCmakeFile(rootCMakePath);

            std::stringstream rootCmakeTemplate;
            rootCmakeTemplate << "# Top-level CMakeLists.txt for CLion/IDE integration\n";
            rootCmakeTemplate << "cmake_minimum_required(VERSION 3.10)\n";
            rootCmakeTemplate << "project(" << projectName << ")\n\n";
            rootCmakeTemplate << "# Define a default build directory for CLion if Presets are ignored\n";
            rootCmakeTemplate << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Builds)\n\n";
            rootCmakeTemplate << "# Include the actual project source definition\n";
            rootCmakeTemplate << "add_subdirectory(Source)\n";

            rootCmakeFile << rootCmakeTemplate.str();
            rootCmakeFile.close();

            // 6. Instantiate the new current project object (UNCHANGED)
            s_CurrentProject = new Project{
                .Name = projectName,
                .RootPath = projectPath,
                .ConfigFilePath = configPath,
                .EngineIncludePath = currentEngineIncludePath
            };

            // 7. Create CMakePresets.json (NEW FOR CLION)
            std::filesystem::path presetsPath = projectPath / "CMakePresets.json";
            std::ofstream presetsFile(presetsPath);

            presetsFile << "{\n";
            presetsFile << "  \"version\": 3,\n";
            presetsFile << "  \"configurePresets\": [\n";
            presetsFile << "    {\n";
            presetsFile << "      \"name\": \"dev-config\",\n";
            presetsFile << "      \"displayName\": \"Engine-Configured Development Build\",\n";
            presetsFile << "      \"generator\": \"Ninja\",\n";
            presetsFile << "      \"binaryDir\": \"${sourceDir}/Builds/cmake\",\n";
            presetsFile << "      \"cacheVariables\": {\n";
            presetsFile << "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
            presetsFile << "        \"ENGINE_INCLUDE_DIR\": \"" << engineIncludePath << "\",\n";
            presetsFile << "        \"GLFW_INCLUDE_DIR\": \"" << glfwIncludePath << "\",\n";
            presetsFile << "        \"GLAD_INCLUDE_DIR\": \"" << gladIncludePath << "\",\n";
            presetsFile << "        \"ENGINE_LIBS_PATHS\": \"" << engineLibPaths << "\"\n";
            presetsFile << "      }\n";
            presetsFile << "    }\n";
            presetsFile << "  ]\n";
            presetsFile << "}\n";

            presetsFile.close();

            LOG_INFO("New project created successfully, ready to build and open in CLion.");
            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Exception during project creation: " + std::string(e.what()));
            return false;
        }
    }


    // --- Build Logic (UNCHANGED from previous fix) ---

    // FIX 8: Correct signature (BuildProject(bool))
    bool ProjectManager::BuildProject(bool playOnFinish) {
        if (!s_CurrentProject) {
            LOG_WARN("Cannot build: No project is currently loaded.");
            return false;
        }

        if (s_CurrentProject->EngineIncludePath.empty() || !std::filesystem::exists(s_CurrentProject->EngineIncludePath)) {
            LOG_ERROR("Build failed: Engine Include Path is missing or invalid in project configuration.");
            return false;
        }

        const std::string projectName = s_CurrentProject->Name;
        const std::filesystem::path projectRoot = s_CurrentProject->RootPath;
        const std::filesystem::path projectSourceDir = projectRoot / "Source";
        const std::filesystem::path tempBuildDir = projectRoot / "TempBuild";

        const std::string engineIncludePath = s_CurrentProject->EngineIncludePath.string();

        // Calculate ALL Paths
        std::filesystem::path zuesEngineRoot = GetZuesEngineRoot();
        std::filesystem::path engineRoot = zuesEngineRoot / "Engine";

        std::filesystem::path glfwIncludeDir = engineRoot / "extern/glfw/include";
        std::filesystem::path gladIncludeDir = engineRoot / "extern/glad/include";

        std::filesystem::path engineLib = engineRoot / "cmake-build-debug/libEngine.a";
        std::filesystem::path glfwLib = engineRoot / "extern/glfw/cmake-build-debug/src/libglfw3.a";
        std::filesystem::path gladLib = engineRoot / "extern/glad/cmake-build-debug/libglad.a";
        std::filesystem::path enetLib = engineRoot / "extern/enet/cmake-build-debug/libenet.a";

        const std::string engineLibPaths = engineLib.string() + ";"
                                         + glfwLib.string() + ";"
                                         + gladLib.string() + ";"
                                         + enetLib.string();

        // 1. Create and clean the temporary build directory (UNCHANGED)
        try {
            if (std::filesystem::exists(tempBuildDir)) {
                LOG_INFO("Cleaning temporary build directory: " + tempBuildDir.string());
                std::filesystem::remove_all(tempBuildDir);
            }
            std::filesystem::create_directory(tempBuildDir);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to set up temporary build directory: " + std::string(e.what()));
            return false;
        }

        LOG_INFO("--- Starting Project Build: " + projectName + " (Play on Finish: " + (playOnFinish ? "Yes" : "No") + ") ---");

        // Helper lambda to execute a command and capture output to the logger
        auto executeAndLog = [](const std::string& command) -> int {
#ifdef _WIN32
            // Redirect stderr to stdout so we capture everything
            std::string fullCommand = command + " 2>&1";
            FILE* pipe = _popen(fullCommand.c_str(), "r");
            if (!pipe) {
                LOG_ERROR("Failed to execute command: " + command);
                return -1;
            }

            char buffer[256];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line(buffer);
                // Remove trailing newline
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                    line.pop_back();
                }
                if (!line.empty()) {
                    // Check if it's an error/warning line
                    if (line.find("error") != std::string::npos || line.find("Error") != std::string::npos) {
                        LOG_ERROR("[Build] " + line);
                    } else if (line.find("warning") != std::string::npos || line.find("Warning") != std::string::npos) {
                        LOG_WARN("[Build] " + line);
                    } else {
                        LOG_INFO("[Build] " + line);
                    }
                }
            }

            return _pclose(pipe);
#else
            return std::system(command.c_str());
#endif
        };

        // 2. CMake Configure Step
        std::string cmakeConfigureCommand =
             "cmake -S \"" + projectSourceDir.string() + "\" -B \"" + tempBuildDir.string() + "\""
             + " -DENGINE_INCLUDE_DIR=\"" + engineIncludePath + "\""
             + " -DENGINE_LIBS_PATHS=\"" + engineLibPaths + "\""
             + " -DGLFW_INCLUDE_DIR=\"" + glfwIncludeDir.string() + "\""
             + " -DGLAD_INCLUDE_DIR=\"" + gladIncludeDir.string() + "\"";

        LOG_INFO("Executing configure command: " + cmakeConfigureCommand);
        int result = executeAndLog(cmakeConfigureCommand);

        if (result != 0) {
            LOG_ERROR("Project build failed: CMake Configure step failed with exit code " + std::to_string(result));
            return false;
        }

        // 3. CMake Build Step
        std::string cmakeBuildCommand =
             "cmake --build \"" + tempBuildDir.string() + "\" --target " + projectName + " --config Release";

        LOG_INFO("Executing build command: " + cmakeBuildCommand);

        result = executeAndLog(cmakeBuildCommand);

        const std::filesystem::path buildOutPath = projectRoot / "Builds" / (projectName + ".exe");
        const std::string buildOutPathStr = buildOutPath.string();

        if (result == 0) {
            LOG_INFO("Project built successfully!");
            LOG_INFO("Executable Location: " + buildOutPathStr);

            // 4. Play on Finish Logic
            if (playOnFinish) {
                if (std::filesystem::exists(buildOutPath)) {
                    LOG_INFO("Starting executable: " + buildOutPathStr);
                    std::string runCommand = "start \"\" \"" + buildOutPathStr + "\"";
                    std::system(runCommand.c_str());
                } else {
                    LOG_ERROR("Could not find executable to run: " + buildOutPathStr);
                }
            }

            return true;
        } else {
            LOG_ERROR("Project build failed: Compilation step returned exit code " + std::to_string(result));
            return false;
        }
    }

    // FIX 9: Correct signature (BuildProjectAsync(bool))
    bool ProjectManager::BuildProjectAsync(const bool playOnFinish) {
        std::lock_guard<std::mutex> lock(s_BuildMutex);
        if (s_IsBuilding) {
            LOG_WARN("Build already in progress.");
            return false;
        }

        if (!s_CurrentProject) {
            LOG_WARN("Cannot build: No project loaded.");
            return false;
        }

        // Store the parameter for the static thread function to access
        ProjectManager::s_BuildParams.PlayOnFinish = playOnFinish;

        s_IsBuilding = true;
        std::thread(BuildProjectThread).detach();
        return true;
    }

    // FIX 10: Correct BuildProject call in thread function
    void ProjectManager::BuildProjectThread() {
        // Retrieve the stored parameter
        const bool playOnFinish = ProjectManager::s_BuildParams.PlayOnFinish;

        // Call the synchronous build with the parameter
        const bool success = BuildProject(playOnFinish);

        s_IsBuilding = false;

        if (success)
            LOG_INFO("Asynchronous build finished successfully!");
        else
            LOG_ERROR("Asynchronous build finished with errors.");
    }

    std::filesystem::path CalculateEngineLibrariesPaths() {
        const std::filesystem::path zuesEngineRoot = GetZuesEngineRoot();
        const std::filesystem::path engineRoot = zuesEngineRoot / "Engine";

        const std::filesystem::path engineLib = engineRoot / "cmake-build-debug/libEngine.a";
        const std::filesystem::path glfwLib = engineRoot / "extern/glfw/cmake-build-debug/src/libglfw3.a";
        const std::filesystem::path gladLib = engineRoot / "extern/glad/cmake-build-debug/libglad.a";
        const std::filesystem::path enetLib = engineRoot / "extern/enet/cmake-build-debug/libenet.a";

        // Replicate the semicolon-separated string format required by the Source/CMakeLists.txt
        return engineLib.string() + ";" + glfwLib.string() + ";" + gladLib.string() + ";" + enetLib.string();
    }

    std::filesystem::path CalculateGLFWIncludePath() {
        const std::filesystem::path zuesEngineRoot = GetZuesEngineRoot();
        const std::filesystem::path engineRoot = zuesEngineRoot / "Engine";
        return engineRoot / "extern/glfw/include";
    }

    std::filesystem::path CalculateGLADIncludePath() {
        const std::filesystem::path zuesEngineRoot = GetZuesEngineRoot();
        const std::filesystem::path engineRoot = zuesEngineRoot / "Engine";
        return engineRoot / "extern/glad/include";
    }

}
