#pragma once
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
        // Attempt to open a project at the given path.
        // Creates a new project structure and necessary files if none exists.
        // Returns true on success, false on critical failure.
        static bool OpenOrCreate(const std::filesystem::path& projectPath);

        // Accessor for the currently loaded project
        static Project* GetCurrent() { return s_CurrentProject; }

        // Function to execute the external build process for the current project.
        // Outputs the executable to [ProjectRoot]/Builds.
        static bool BuildProject();

    private:
        static Project* s_CurrentProject; // Stores the active project instance
        static const std::string CONFIG_FILE_NAME; // Name of the file that identifies a project

        static bool CreateNewProject(const std::filesystem::path& projectPath);
        static bool LoadExistingProject(const std::filesystem::path& projectPath);
    };
}