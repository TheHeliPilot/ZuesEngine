#include "../include/ProjectManager.h"
#include "EngineDefines.h"
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

#include "Core.h"

namespace Engine {
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
            const std::filesystem::path currentPath = std::filesystem::current_path();
            // From Editor/cmake-build-debug/bin, go up 3 levels to ZuesEngine root
            const std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path().parent_path();
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
        if (!Core::LoadWorld(projectPath.string() + "/Worlds/World.json")) {
            LOG_WARN("Failed to load world at " + projectPath.string() + "/Worlds/World.json. Starting with empty world.");
            // TODO: Fix DLL boundary issues with world serialization before re-enabling save
            // if (!Core::SaveWorld(projectPath.string() + "/Worlds/")) {
            //     LOG_ERROR("Failed to save world!");
            // }
        }else LOG_INFO("Loaded world " + projectPath.string() + "/Worlds/World.json");
        return true;
    }

    // Helper to replace all occurrences of a placeholder in a string
    void ReplacePlaceholder(std::string& str, const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = str.find(placeholder, pos)) != std::string::npos) {
            str.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }

    bool ProjectManager::CreateNewProject(const std::filesystem::path& projectPath) {
        try {
            // --- 0. Path Setup & Calculation ---
            std::string projectName = projectPath.filename().string();
            std::filesystem::path sourcePath = projectPath / "Source";

            std::filesystem::path currentPath = std::filesystem::current_path();
            // From Editor/cmake-build-debug/bin, go up 3 levels to ZuesEngine root
            std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path().parent_path();
            std::filesystem::path engineRoot = zuesEngineRoot / "Engine";

            // Calculate all paths needed for the project
            std::filesystem::path currentEngineIncludePath = CalculateEngineIncludePath();
            if (currentEngineIncludePath.empty()) return false;

            std::string glfwIncludePath = (engineRoot / "extern/glfw/include").string();
            std::string gladIncludePath = (engineRoot / "extern/glad/include").string();
            std::string engineIncludePath = currentEngineIncludePath.string();

            // ZuesEngine DLL path (new DLL-based architecture)
            std::string zuesEngineLibPath = (engineRoot / "cmake-build-debug/bin/ZuesEngine.lib").string();

            // Replace Windows backslashes with forward slashes for CMake/JSON compatibility
            auto replace_slashes = [](std::string& s) {
                for (char& c : s) { if (c == '\\') c = '/'; }
            };

            replace_slashes(engineIncludePath);
            replace_slashes(glfwIncludePath);
            replace_slashes(gladIncludePath);
            replace_slashes(zuesEngineLibPath);

            // 1. Create Directories
            if (!std::filesystem::create_directories(projectPath) && !std::filesystem::exists(projectPath)) {
                LOG_ERROR("Failed to create project root directory: " + projectPath.string());
                return false;
            }

            std::filesystem::create_directory(projectPath / "Assets");
            std::filesystem::create_directory(projectPath / "Worlds");
            std::filesystem::create_directory(sourcePath);
            std::filesystem::create_directory(projectPath / "Builds");


            // 2. Create config file
            std::filesystem::path configPath = projectPath / CONFIG_FILE_NAME;
            std::ofstream configFile(configPath);
            configFile << "Name=" << projectName << "\n";
            configFile << ProjectManager::ENGINE_INCLUDE_KEY << currentEngineIncludePath.string() << "\n";
            configFile.close();

            // 3. Create main C++ source file from template
            std::filesystem::path mainCppPath = sourcePath / (projectName + ".cpp");
            std::filesystem::path cppTemplatePath = zuesEngineRoot / "Editor/Templates/GameProject.cpp.template";

            if (!std::filesystem::exists(cppTemplatePath)) {
                LOG_ERROR("FATAL: C++ template file not found. Ensure it exists at: " + cppTemplatePath.string());
                return false;
            }

            std::ifstream cppTemplateFile(cppTemplatePath);
            std::stringstream cppBuffer;
            cppBuffer << cppTemplateFile.rdbuf();
            std::string mainCppContent = cppBuffer.str();
            cppTemplateFile.close();

            ReplacePlaceholder(mainCppContent, "{{PROJECT_NAME}}", projectName);

            std::ofstream mainCppFile(mainCppPath);
            mainCppFile << mainCppContent;
            mainCppFile.close();

            // 4. Create Source/CMakeLists.txt from template
            std::filesystem::path cmakeTemplatePath = zuesEngineRoot / "Editor/Templates/CMakeLists.txt.template";
            std::filesystem::path cmakePath = sourcePath / "CMakeLists.txt";

            if (!std::filesystem::exists(cmakeTemplatePath)) {
                LOG_ERROR("FATAL: CMakeLists.txt template not found. Ensure it exists at: " + cmakeTemplatePath.string());
                return false;
            }

            std::ifstream cmakeTemplateFile(cmakeTemplatePath);
            std::stringstream cmakeBuffer;
            cmakeBuffer << cmakeTemplateFile.rdbuf();
            std::string cmakeContent = cmakeBuffer.str();
            cmakeTemplateFile.close();

            // Replace all placeholders in CMakeLists.txt template
            ReplacePlaceholder(cmakeContent, "{{PROJECT_NAME}}", projectName);
            ReplacePlaceholder(cmakeContent, "{{ENGINE_INCLUDE_DIR}}", engineIncludePath);
            ReplacePlaceholder(cmakeContent, "{{GLFW_INCLUDE_DIR}}", glfwIncludePath);
            ReplacePlaceholder(cmakeContent, "{{GLAD_INCLUDE_DIR}}", gladIncludePath);
            ReplacePlaceholder(cmakeContent, "{{ZUES_ENGINE_LIB}}", zuesEngineLibPath);

            std::ofstream cmakeFile(cmakePath);
            cmakeFile << cmakeContent;
            cmakeFile.close();

            // 5. Create Top-Level CMakeLists.txt for CLion/IDE integration
            std::filesystem::path rootCMakePath = projectPath / "CMakeLists.txt";
            std::ofstream rootCmakeFile(rootCMakePath);

            rootCmakeFile << "# Top-level CMakeLists.txt for CLion/IDE integration\n";
            rootCmakeFile << "cmake_minimum_required(VERSION 3.10)\n";
            rootCmakeFile << "project(" << projectName << ")\n\n";
            rootCmakeFile << "# Define a default build directory for CLion if Presets are ignored\n";
            rootCmakeFile << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Builds)\n\n";
            rootCmakeFile << "# Include the actual project source definition\n";
            rootCmakeFile << "add_subdirectory(Source)\n";

            rootCmakeFile.close();

            // 6. Instantiate the new current project object
            s_CurrentProject = new Project{
                .Name = projectName,
                .RootPath = projectPath,
                .ConfigFilePath = configPath,
                .EngineIncludePath = currentEngineIncludePath
            };

            // 7. Create CMakePresets.json for CLion with both DLL and EXE presets
            std::filesystem::path presetsPath = projectPath / "CMakePresets.json";
            std::ofstream presetsFile(presetsPath);

            presetsFile << "{\n";
            presetsFile << "  \"version\": 3,\n";
            presetsFile << "  \"configurePresets\": [\n";
            presetsFile << "    {\n";
            presetsFile << "      \"name\": \"dll-debug\",\n";
            presetsFile << "      \"displayName\": \"DLL Build (Hot-Reload)\",\n";
            presetsFile << "      \"generator\": \"Ninja\",\n";
            presetsFile << "      \"binaryDir\": \"${sourceDir}/Builds/cmake-dll\",\n";
            presetsFile << "      \"cacheVariables\": {\n";
            presetsFile << "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
            presetsFile << "        \"BUILD_AS_DLL\": \"ON\",\n";
            presetsFile << "        \"ENGINE_INCLUDE_DIR\": \"" << engineIncludePath << "\",\n";
            presetsFile << "        \"GLFW_INCLUDE_DIR\": \"" << glfwIncludePath << "\",\n";
            presetsFile << "        \"GLAD_INCLUDE_DIR\": \"" << gladIncludePath << "\",\n";
            presetsFile << "        \"ZUES_ENGINE_LIB\": \"" << zuesEngineLibPath << "\"\n";
            presetsFile << "      }\n";
            presetsFile << "    },\n";
            presetsFile << "    {\n";
            presetsFile << "      \"name\": \"exe-debug\",\n";
            presetsFile << "      \"displayName\": \"EXE Build (Standalone)\",\n";
            presetsFile << "      \"generator\": \"Ninja\",\n";
            presetsFile << "      \"binaryDir\": \"${sourceDir}/Builds/cmake-exe\",\n";
            presetsFile << "      \"cacheVariables\": {\n";
            presetsFile << "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
            presetsFile << "        \"BUILD_AS_DLL\": \"OFF\",\n";
            presetsFile << "        \"ENGINE_INCLUDE_DIR\": \"" << engineIncludePath << "\",\n";
            presetsFile << "        \"GLFW_INCLUDE_DIR\": \"" << glfwIncludePath << "\",\n";
            presetsFile << "        \"GLAD_INCLUDE_DIR\": \"" << gladIncludePath << "\",\n";
            presetsFile << "        \"ZUES_ENGINE_LIB\": \"" << zuesEngineLibPath << "\"\n";
            presetsFile << "      }\n";
            presetsFile << "    },\n";
            presetsFile << "    {\n";
            presetsFile << "      \"name\": \"exe-release\",\n";
            presetsFile << "      \"displayName\": \"EXE Build (Release)\",\n";
            presetsFile << "      \"generator\": \"Ninja\",\n";
            presetsFile << "      \"binaryDir\": \"${sourceDir}/Builds/cmake-release\",\n";
            presetsFile << "      \"cacheVariables\": {\n";
            presetsFile << "        \"CMAKE_BUILD_TYPE\": \"Release\",\n";
            presetsFile << "        \"BUILD_AS_DLL\": \"OFF\",\n";
            presetsFile << "        \"ENGINE_INCLUDE_DIR\": \"" << engineIncludePath << "\",\n";
            presetsFile << "        \"GLFW_INCLUDE_DIR\": \"" << glfwIncludePath << "\",\n";
            presetsFile << "        \"GLAD_INCLUDE_DIR\": \"" << gladIncludePath << "\",\n";
            presetsFile << "        \"ZUES_ENGINE_LIB\": \"" << zuesEngineLibPath << "\"\n";
            presetsFile << "      }\n";
            presetsFile << "    }\n";
            presetsFile << "  ]\n";
            presetsFile << "}\n";

            presetsFile.close();

            LOG_INFO("New project created successfully with DLL hot-reload support.");
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
        std::filesystem::path currentPath = std::filesystem::current_path();
        // From Editor/cmake-build-debug/bin, go up 3 levels to ZuesEngine root
        std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path().parent_path();
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

        // 2. CMake Configure Step (UNCHANGED)
        std::string cmakeConfigureCommand =
             "cmake -S \"" + projectSourceDir.string() + "\" -B \"" + tempBuildDir.string() + "\""
             + " -DENGINE_INCLUDE_DIR=\"" + engineIncludePath + "\""
             + " -DENGINE_LIBS_PATHS=\"" + engineLibPaths + "\""
             + " -DGLFW_INCLUDE_DIR=\"" + glfwIncludeDir.string() + "\""
             + " -DGLAD_INCLUDE_DIR=\"" + gladIncludeDir.string() + "\"";

        LOG_INFO("Executing configure command: " + cmakeConfigureCommand);
        int result = std::system(cmakeConfigureCommand.c_str());

        if (result != 0) {
            LOG_ERROR("Project build failed: CMake Configure step failed. Check console output.");
            return false;
        }

        // 3. CMake Build Step (UNCHANGED)
        std::string cmakeBuildCommand =
             "cmake --build \"" + tempBuildDir.string() + "\" --target " + projectName + " --config Release";

        LOG_INFO("Executing build command: " + cmakeBuildCommand);

        result = std::system(cmakeBuildCommand.c_str());

        const std::filesystem::path buildOutPath = projectRoot / "Builds" / (projectName + ".exe");
        const std::string buildOutPathStr = buildOutPath.string();

        if (result == 0) {
            LOG_INFO("Project built successfully! 🎉");
            LOG_INFO("Executable Location: " + buildOutPathStr);

            // 4. Play on Finish Logic (NEW)
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
            LOG_ERROR("Project build failed: Compilation step returned a non-zero exit code. Check console for CMake/Compiler errors.");
            return false;
        }
    }

    // FIX 9: Correct signature (BuildProjectAsync(bool))
    bool ProjectManager::BuildProjectAsync(bool playOnFinish) {
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
        const std::filesystem::path currentPath = std::filesystem::current_path();
        // From Editor/cmake-build-debug/bin, go up 3 levels to ZuesEngine root
        const std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path().parent_path();
        const std::filesystem::path engineRoot = zuesEngineRoot / "Engine";

        const std::filesystem::path engineLib = engineRoot / "cmake-build-debug/libEngine.a";
        const std::filesystem::path glfwLib = engineRoot / "extern/glfw/cmake-build-debug/src/libglfw3.a";
        const std::filesystem::path gladLib = engineRoot / "extern/glad/cmake-build-debug/libglad.a";
        const std::filesystem::path enetLib = engineRoot / "extern/enet/cmake-build-debug/libenet.a";

        // Replicate the semicolon-separated string format required by the Source/CMakeLists.txt
        return engineLib.string() + ";" + glfwLib.string() + ";" + gladLib.string() + ";" + enetLib.string();
    }

    std::filesystem::path CalculateGLFWIncludePath() {
        const std::filesystem::path currentPath = std::filesystem::current_path();
        // From Editor/cmake-build-debug/bin, go up 3 levels to ZuesEngine root
        const std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path().parent_path();
        const std::filesystem::path engineRoot = zuesEngineRoot / "Engine";
        return engineRoot / "extern/glfw/include";
    }

    std::filesystem::path CalculateGLADIncludePath() {
        const std::filesystem::path currentPath = std::filesystem::current_path();
        // From Editor/cmake-build-debug/bin, go up 3 levels to ZuesEngine root
        const std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path().parent_path();
        const std::filesystem::path engineRoot = zuesEngineRoot / "Engine";
        return engineRoot / "extern/glad/include";
    }

}
