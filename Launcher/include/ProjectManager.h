#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace Zues {

struct ProjectInfo {
    std::string name;
    std::string path;
    std::string lastOpened;
    std::string version;
    bool valid = false;
};

class LauncherProjectManager {
public:
    LauncherProjectManager();

    // Load projects from config file
    void LoadProjects();

    // Save projects to config file
    void SaveProjects();

    // Get list of all known projects
    const std::vector<ProjectInfo>& GetProjects() const { return m_Projects; }

    // Add a project to the list (by scanning directory for project.zues)
    bool AddProject(const std::string& projectPath);

    // Remove a project from the list (does not delete files)
    void RemoveProject(const std::string& projectPath);

    // Open a project in the editor
    bool OpenProject(const ProjectInfo& project);

    // Create a new project from template
    bool CreateProject(const std::string& name, const std::string& location);

    // Validate a project (check if project.zues exists and is valid)
    static std::optional<ProjectInfo> ValidateProject(const std::string& path);

    // Get the launcher config directory
    static std::filesystem::path GetConfigDir();

    // Get the editor executable path
    std::string GetEditorPath() const;

    // Get the launcher executable directory
    static std::filesystem::path GetLauncherDirectory();

private:
    std::vector<ProjectInfo> m_Projects;
    std::filesystem::path m_ConfigPath;
};

} // namespace Zues
