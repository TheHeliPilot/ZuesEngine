#pragma once
#include <atomic>
#include <string>
#include <filesystem>

namespace Engine {

    // Structure to hold runtime project information
    struct Project {
        std::string Name;
        std::filesystem::path RootPath;
        std::filesystem::path ConfigFilePath;
        // NEW: Store the absolute path to the Engine's public headers
        std::filesystem::path EngineIncludePath;
        // Add other metadata later (e.g., startScenePath, assetDirectory)
    };

    class ProjectManager final {
    public:
        // Existing API
        static bool OpenOrCreate(const std::filesystem::path& projectPath);
        static Project* GetCurrent() { return s_CurrentProject; }
        static bool BuildProject(); // synchronous version

        // --- New async API ---
        static bool BuildProjectAsync(); // starts async build

    private:
        static Project* s_CurrentProject;
        static const std::string CONFIG_FILE_NAME;

        static bool CreateNewProject(const std::filesystem::path& projectPath);
        static bool LoadExistingProject(const std::filesystem::path& projectPath);

        // --- New members for async build ---
        static void BuildProjectThread();             // the actual thread function
        static std::atomic<bool> s_IsBuilding;       // true if a build is in progress
        static std::mutex s_BuildMutex;              // protects starting new builds
    };

}