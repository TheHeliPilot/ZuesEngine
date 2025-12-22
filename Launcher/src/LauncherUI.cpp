#include "LauncherUI.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#include <thread>
#include <chrono>

namespace Zues {

LauncherUI::LauncherUI() = default;

LauncherUI::~LauncherUI() {
    Shutdown();
}

bool LauncherUI::Initialize() {
    if (!glfwInit()) {
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_Window = glfwCreateWindow(m_WindowWidth, m_WindowHeight, "Zues Engine Launcher", nullptr, nullptr);
    if (!m_Window) {
        glfwTerminate();
        return false;
    }

    CenterWindow();

    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(m_Window);
        glfwTerminate();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyStyle();

    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    return true;
}

void LauncherUI::Run() {
    while (!glfwWindowShouldClose(m_Window) && !m_ShouldClose) {
        glfwPollEvents();
        RenderFrame();
    }
}

void LauncherUI::Shutdown() {
    if (m_Window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(m_Window);
        glfwTerminate();
        m_Window = nullptr;
    }
}

void LauncherUI::RenderFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Main window covers entire screen
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)m_WindowWidth, (float)m_WindowHeight));
    ImGui::Begin("Launcher", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Header
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::SetCursorPosX((m_WindowWidth - ImGui::CalcTextSize("ZUES ENGINE").x) * 0.5f);
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "ZUES ENGINE");
    ImGui::PopFont();

    ImGui::Separator();
    ImGui::Spacing();

    switch (m_State) {
        case LauncherState::CheckingPrerequisites:
            RenderPrerequisitesCheck();
            break;
        case LauncherState::PrerequisitesMissing:
            RenderPrerequisitesMissing();
            break;
        case LauncherState::ProjectList:
            RenderProjectList();
            break;
        case LauncherState::CreatingProject:
            RenderCreateProject();
            break;
    }

    ImGui::End();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(m_Window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_Window);
}

void LauncherUI::RenderPrerequisitesCheck() {
    if (!m_PrerequisitesChecked && !m_PrerequisiteCheckInProgress) {
        ImGui::Text("Checking prerequisites...");
        ImGui::Spacing();

        // Start async check
        m_PrerequisiteCheckInProgress = true;
        std::thread([this]() {
            m_PrerequisiteResult = Prerequisites::CheckAll();
            m_PrerequisitesChecked = true;
            m_PrerequisiteCheckInProgress = false;
        }).detach();
    } else if (m_PrerequisiteCheckInProgress) {
        ImGui::Text("Checking prerequisites...");
        ImGui::Spacing();
    } else if (m_PrerequisitesChecked) {
        if (m_PrerequisiteResult.allMet) {
            TransitionTo(LauncherState::ProjectList);
        } else {
            TransitionTo(LauncherState::PrerequisitesMissing);
        }
    }
}

void LauncherUI::RenderPrerequisitesMissing() {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Some prerequisites are missing:");
    ImGui::Spacing();
    ImGui::Spacing();

    for (const auto& prereq : m_PrerequisiteResult.prerequisites) {
        ImGui::PushID(prereq.name.c_str());

        bool ok = prereq.installed && prereq.versionOk;
        ImVec4 color = ok ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        const char* icon = ok ? "[OK]" : "[MISSING]";

        ImGui::TextColored(color, "%s", icon);
        ImGui::SameLine();
        ImGui::Text("%s", prereq.name.c_str());

        if (prereq.installed) {
            ImGui::SameLine();
            ImGui::TextDisabled("v%s at %s", prereq.version.c_str(), prereq.path.c_str());
        }

        if (!ok) {
            ImGui::Indent();
            ImGui::TextWrapped("%s", prereq.installInstructions.c_str());

            if (ImGui::Button("Download")) {
                Prerequisites::OpenURL(prereq.downloadUrl);
            }

            ImGui::Unindent();
        }

        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Re-check Prerequisites")) {
        m_PrerequisitesChecked = false;
        m_PrerequisiteCheckInProgress = false;
        TransitionTo(LauncherState::CheckingPrerequisites);
    }

    ImGui::SameLine();

    if (ImGui::Button("Continue Anyway")) {
        TransitionTo(LauncherState::ProjectList);
    }
}

void LauncherUI::RenderProjectList() {
    // Buttons row
    if (ImGui::Button("New Project")) {
        m_NewProjectName[0] = '\0';
        m_NewProjectPath[0] = '\0';
        m_CreateProjectError.clear();
        TransitionTo(LauncherState::CreatingProject);
    }

    ImGui::SameLine();

    if (ImGui::Button("Add Existing Project")) {
#ifdef _WIN32
        BROWSEINFOA bi = {0};
        bi.lpszTitle = "Select Project Folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl != nullptr) {
            char path[MAX_PATH];
            if (SHGetPathFromIDListA(pidl, path)) {
                if (!m_ProjectManager.AddProject(path)) {
                    // Show error
                }
            }
            CoTaskMemFree(pidl);
        }
#endif
    }

    ImGui::SameLine();

    if (ImGui::Button("Refresh")) {
        m_ProjectManager.LoadProjects();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Projects list
    const auto& projects = m_ProjectManager.GetProjects();

    if (projects.empty()) {
        ImGui::TextDisabled("No projects found. Create a new project or add an existing one.");
    } else {
        ImGui::BeginChild("ProjectsList", ImVec2(0, -50), true);

        for (size_t i = 0; i < projects.size(); ++i) {
            const auto& project = projects[i];
            ImGui::PushID(static_cast<int>(i));

            // Use BeginGroup/EndGroup for proper layout
            ImGui::BeginGroup();

            // Draw project info as regular text items
            ImGui::Indent(10.0f);
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", project.name.c_str());
            ImGui::TextDisabled("%s", project.path.c_str());
            ImGui::TextDisabled("Last opened: %s", project.lastOpened.c_str());
            ImGui::Unindent(10.0f);

            ImGui::EndGroup();

            // Make the group interactive
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }

            if (ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(0)) {
                if (m_ProjectManager.OpenProject(project)) {
                    m_ShouldClose = true;
                }
            }

            // Context menu
            if (ImGui::BeginPopupContextItem("ProjectContext")) {
                if (ImGui::MenuItem("Open")) {
                    if (m_ProjectManager.OpenProject(project)) {
                        m_ShouldClose = true;
                    }
                }
                if (ImGui::MenuItem("Open Folder")) {
                    Prerequisites::OpenFolder(project.path);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove from List")) {
                    m_ProjectManager.RemoveProject(project.path);
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    // Footer
    ImGui::SetCursorPosY(m_WindowHeight - 40);
    ImGui::TextDisabled("Double-click a project to open it in the editor");
}

void LauncherUI::RenderCreateProject() {
    ImGui::Text("Create New Project");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Check if async project creation finished
    if (m_ProjectCreationComplete) {
        if (m_ProjectCreationSuccess) {
            TransitionTo(LauncherState::ProjectList);
        } else {
            m_CreateProjectError = "Failed to create project. Check that the editor is available.";
        }
        m_CreatingProject = false;
        m_ProjectCreationComplete = false;
    }

    // Show progress if creating
    if (m_CreatingProject) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("Creating project, please wait...");
        ImGui::Spacing();

        // Simple progress animation
        static float progress = 0.0f;
        progress += 0.02f;
        if (progress > 1.0f) progress = 0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 0), "");

        ImGui::Spacing();
        ImGui::TextDisabled("The editor is setting up your project structure...");
        return;
    }

    ImGui::Text("Project Name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", m_NewProjectName, sizeof(m_NewProjectName));

    ImGui::Spacing();

    ImGui::Text("Location:");
    ImGui::SetNextItemWidth(-100);
    ImGui::InputText("##path", m_NewProjectPath, sizeof(m_NewProjectPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
#ifdef _WIN32
        BROWSEINFOA bi = {0};
        bi.lpszTitle = "Select Project Location";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl != nullptr) {
            char path[MAX_PATH];
            if (SHGetPathFromIDListA(pidl, path)) {
                strncpy_s(m_NewProjectPath, path, sizeof(m_NewProjectPath) - 1);
            }
            CoTaskMemFree(pidl);
        }
#endif
    }

    if (!m_CreateProjectError.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_CreateProjectError.c_str());
    }

    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Create")) {
        if (strlen(m_NewProjectName) == 0) {
            m_CreateProjectError = "Please enter a project name";
        } else if (strlen(m_NewProjectPath) == 0) {
            m_CreateProjectError = "Please select a location";
        } else {
            // Start async project creation
            m_CreateProjectError.clear();
            m_CreatingProject = true;
            m_ProjectCreationComplete = false;

            // Launch creation in a separate thread
            std::thread([this]() {
                bool success = m_ProjectManager.CreateProject(m_NewProjectName, m_NewProjectPath);
                m_ProjectCreationSuccess = success;
                m_ProjectCreationComplete = true;
            }).detach();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        TransitionTo(LauncherState::ProjectList);
    }
}

void LauncherUI::TransitionTo(LauncherState state) {
    m_State = state;
}

void LauncherUI::CenterWindow() {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    int x = (mode->width - m_WindowWidth) / 2;
    int y = (mode->height - m_WindowHeight) / 2;

    glfwSetWindowPos(m_Window, x, y);
}

void LauncherUI::ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 8);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
}

} // namespace Zues
