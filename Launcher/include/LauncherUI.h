#pragma once

#include "Prerequisites.h"
#include "ProjectManager.h"

struct GLFWwindow;

namespace Zues {

enum class LauncherState {
    CheckingPrerequisites,
    PrerequisitesMissing,
    ProjectList,
    CreatingProject
};

class LauncherUI {
public:
    LauncherUI();
    ~LauncherUI();

    // Initialize GLFW and ImGui
    bool Initialize();

    // Main loop
    void Run();

    // Cleanup
    void Shutdown();

private:
    // UI Rendering
    void RenderFrame();
    void RenderPrerequisitesCheck();
    void RenderPrerequisitesMissing();
    void RenderProjectList();
    void RenderCreateProject();

    // State management
    void TransitionTo(LauncherState state);

    // Helpers
    void CenterWindow();
    void ApplyStyle();

private:
    GLFWwindow* m_Window = nullptr;
    LauncherState m_State = LauncherState::CheckingPrerequisites;

    // Prerequisites
    PrerequisiteCheckResult m_PrerequisiteResult;
    bool m_PrerequisitesChecked = false;
    bool m_PrerequisiteCheckInProgress = false;

    // Project management
    LauncherProjectManager m_ProjectManager;

    // Create project dialog
    char m_NewProjectName[256] = "";
    char m_NewProjectPath[512] = "";
    std::string m_CreateProjectError;
    bool m_CreatingProject = false;
    bool m_ProjectCreationComplete = false;
    bool m_ProjectCreationSuccess = false;

    // Window dimensions
    int m_WindowWidth = 800;
    int m_WindowHeight = 600;

    bool m_ShouldClose = false;
};

} // namespace Zues
