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

#include "../include/Engine/Core.h"

namespace Engine {
    // Initialize static members (All declared in the fixed header)
    Project* ProjectManager::s_CurrentProject = nullptr;
    const std::string ProjectManager::CONFIG_FILE_NAME = "project.zues";
    const std::string ProjectManager::ENGINE_INCLUDE_KEY = "EngineIncludePath=";

    std::atomic<bool> ProjectManager::s_IsBuilding = false;
    std::mutex ProjectManager::s_BuildMutex;

    // CRITICAL FIX 4: Initialization of the static member s_BuildParams
    BuildParams ProjectManager::s_BuildParams;


    // --- Helper 1: Dynamically calculate Engine Include Path --- (UNCHANGED)
    std::filesystem::path CalculateEngineIncludePath() {
        try {
            std::filesystem::path currentPath = std::filesystem::current_path();
            std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path();
            std::filesystem::path engineIncludePath = zuesEngineRoot / "Engine/include/Engine";

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
        std::string projectName = projectPath.filename().string();
        std::filesystem::path configPath = projectPath / CONFIG_FILE_NAME;

        std::filesystem::path currentEngineIncludePath = CalculateEngineIncludePath();
        if (currentEngineIncludePath.empty()) return false;

        WriteEnginePathToConfig(configPath, currentEngineIncludePath);

        s_CurrentProject = new Project{
            .Name = projectName,
            .RootPath = projectPath,
            .ConfigFilePath = configPath,
            .EngineIncludePath = currentEngineIncludePath
        };

        LOG_INFO("Project loaded successfully: " + projectName);
        if (!Core::LoadWorld(projectPath.string() + "/Scenes/World.json")) {
            LOG_WARN("Failed to load world at " + projectPath.string() + "/Scenes/World.json. Saving default.");
            if (!Core::SaveWorld(projectPath.string() + "/Scenes/")) {
                LOG_ERROR("Failed to save world!");
            }
        }else LOG_INFO("Loaded world " + projectPath.string() + "/Scenes/World.json");
        return true;
    }

    bool ProjectManager::CreateNewProject(const std::filesystem::path& projectPath) {
        try {
            // 1. Create Directories (UNCHANGED)
            if (!std::filesystem::create_directories(projectPath) && !std::filesystem::exists(projectPath)) {
                LOG_ERROR("Failed to create project root directory: " + projectPath.string());
                return false;
            }

            std::string projectName = projectPath.filename().string();
            std::filesystem::path sourcePath = projectPath / "Source";

            std::filesystem::create_directory(projectPath / "Assets");
            std::filesystem::create_directory(projectPath / "Scenes");
            std::filesystem::create_directory(sourcePath);
            std::filesystem::create_directory(projectPath / "Builds");

            // 2. Calculate Engine Path (UNCHANGED)
            std::filesystem::path currentEngineIncludePath = CalculateEngineIncludePath();
            if (currentEngineIncludePath.empty()) return false;

            // 3. Create config file
            std::filesystem::path configPath = projectPath / CONFIG_FILE_NAME;
            std::ofstream configFile(configPath);
            configFile << "Name=" << projectName << "\n";
            // FIX 7: Use ProjectManager::ENGINE_INCLUDE_KEY
            configFile << ProjectManager::ENGINE_INCLUDE_KEY << currentEngineIncludePath.string() << "\n";
            configFile.close();

            // 4. Create main C++ source file (UNCHANGED)
            std::filesystem::path mainCppPath = sourcePath / (projectName + ".cpp");
            std::filesystem::path currentPath = std::filesystem::current_path();
            std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path();
            std::filesystem::path cppTemplatePath = zuesEngineRoot / "Editor/Templates/GameProject.cpp.template";

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

            // 5. Create Project CMakeLists.txt (UNCHANGED)
            std::filesystem::path cmakePath = sourcePath / "CMakeLists.txt";
            std::ofstream cmakeFile(cmakePath);

            std::stringstream cmakeTemplate;
            cmakeTemplate << "# Project-specific CMakeLists.txt for " << projectName << "\n";
            cmakeTemplate << "cmake_minimum_required(VERSION 3.10)\n";
            cmakeTemplate << "project(" << projectName << " VERSION 1.0.0)\n\n";

            cmakeTemplate << "set(CMAKE_CXX_STANDARD 23)\n";
            cmakeTemplate << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

            cmakeTemplate << "# Specify the project's main source file\n";
            cmakeTemplate << "set(PROJECT_SOURCES " << projectName << ".cpp)\n\n";

            cmakeTemplate << "# Create the executable\n";
            cmakeTemplate << "add_executable(${PROJECT_NAME} WIN32 ${PROJECT_SOURCES})\n\n";

            cmakeTemplate << "# Engine headers are provided via the ENGINE_INCLUDE_DIR CMake argument.\n";
            cmakeTemplate << "if(NOT ENGINE_INCLUDE_DIR)\n";
            cmakeTemplate << "    message(FATAL_ERROR \"ENGINE_INCLUDE_DIR is not defined. The game project cannot find Engine headers.\")\n";
            cmakeTemplate << "endif()\n";

            cmakeTemplate << "if(NOT GLFW_INCLUDE_DIR OR NOT GLAD_INCLUDE_DIR)\n";
            cmakeTemplate << "    message(FATAL_ERROR \"External library include paths (GLFW/GLAD) are missing. Check ProjectManager::BuildProject for configuration errors.\")\n";
            cmakeTemplate << "endif()\n";

            cmakeTemplate << "target_include_directories(${PROJECT_NAME} PRIVATE \n";
            cmakeTemplate << "    ${ENGINE_INCLUDE_DIR}\n";
            cmakeTemplate << "    ${GLFW_INCLUDE_DIR}\n";
            cmakeTemplate << "    ${GLAD_INCLUDE_DIR}\n";
            cmakeTemplate << ")\n\n";

            cmakeTemplate << "# Absolute library paths provided via the ENGINE_LIBS_PATHS CMake argument.\n";
            cmakeTemplate << "if(NOT ENGINE_LIBS_PATHS)\n";
            cmakeTemplate << "    message(FATAL_ERROR \"ENGINE_LIBS_PATHS is not defined. The game project cannot find Engine library files.\")\n";
            cmakeTemplate << "endif()\n";

            cmakeTemplate << "target_link_libraries(${PROJECT_NAME} PRIVATE\n";
            cmakeTemplate << "    \"${ENGINE_LIBS_PATHS}\"\n";
            cmakeTemplate << "    opengl32\n";
            cmakeTemplate << "    Ws2_32\n";
            cmakeTemplate << "    Gdi32\n";
            cmakeTemplate << "    winmm\n";
            cmakeTemplate << ")\n\n";

            cmakeTemplate << "set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/../Builds\")\n";

            cmakeFile << cmakeTemplate.str();
            cmakeFile.close();

            // 6. Instantiate the new current project object (UNCHANGED)
            s_CurrentProject = new Project{
                .Name = projectName,
                .RootPath = projectPath,
                .ConfigFilePath = configPath,
                .EngineIncludePath = currentEngineIncludePath
            };

            LOG_INFO("New project created successfully, ready to build.");
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

        // Calculate ALL Paths (UNCHANGED)
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path zuesEngineRoot = currentPath.parent_path().parent_path();
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
}
