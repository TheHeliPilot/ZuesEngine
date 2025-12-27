#pragma once
#include <atomic>
#include <string>
#include <filesystem>
#include <mutex>

namespace Engine {

    // FIX 1: Define the BuildParams struct here so the compiler knows the type
    struct BuildParams {
        bool PlayOnFinish;
    };

    // Structure to hold runtime project information
    struct Project {
        std::string Name;
        std::filesystem::path RootPath;
        std::filesystem::path ConfigFilePath;
        std::filesystem::path EngineIncludePath;
        std::string StartupWorld;  // Name of the world to load on startup (without .json extension)
    };

    class ProjectManager final {
    public:
        // Existing static members
        static const std::string ENGINE_INCLUDE_KEY;

        // Existing API
        static bool OpenOrCreate(const std::filesystem::path& projectPath);
        static Project* GetCurrent() { return s_CurrentProject; }

        // FIX 2: Corrected Build function signatures to include the bool parameter
        static bool BuildProject(bool playOnFinish = false);
        static bool BuildProjectAsync(bool playOnFinish = false);
        static bool IsBuilding() { return s_IsBuilding; } // Helper for checking build status

        // Project settings
        static void SetStartupWorld(const std::string& worldName);
        static void SaveProjectSettings();

    private:
        static Project* s_CurrentProject;
        static const std::string CONFIG_FILE_NAME;

        static bool CreateNewProject(const std::filesystem::path& projectPath);
        static bool LoadExistingProject(const std::filesystem::path& projectPath);

        // --- New members for async build ---
        static void BuildProjectThread();
        static std::atomic<bool> s_IsBuilding;
        static std::mutex s_BuildMutex;

        // CRITICAL FIX 3: Declaration of the static member used for async parameters
        static BuildParams s_BuildParams;
    };

}