#include "../include/EditorUi.h"
#include "imgui.h"
#include <unordered_map>
#include "Renderer.h"
#include "Core.h"
#include "ProjectManager.h"
#include "../include/LoggerUI.h"
#include <GLFW/glfw3.h> // Essential for GLFW functions

extern GLFWwindow* g_MainWindow;

using namespace EditorWindows;

std::filesystem::path EditorUi::projectDir = "../../MyGameProject";

Engine::Math::Vec2 EditorUi::viewportMousePos = {-1, -1};
Engine::Math::Vec2 EditorUi::viewportSize = {-1, -1};
static std::unordered_map<std::string, ImVec2> g_WindowPosCache;
static std::unordered_map<std::string, ImVec2> g_WindowSizeCache;

static ImVec2 FromImVec2(const ImVec2& v) { return {v.x, v.y}; }

// --- Custom Title Bar ---
static void DrawCustomTitleBar() {
    constexpr float title_bar_height = 35.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, title_bar_height));
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                                 | ImGuiWindowFlags_NoMove
                                 | ImGuiWindowFlags_NoResize
                                 | ImGuiWindowFlags_NoSavedSettings
                                 | ImGuiWindowFlags_NoDocking
                                 | ImGuiWindowFlags_NoBringToFrontOnFocus
                                 | ImGuiWindowFlags_NoScrollbar;

    const ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_TitleBgActive];
    ImGui::PushStyleColor(ImGuiCol_WindowBg, accent);
    ImGui::Begin("##CustomTitleBar", nullptr, flags);
    ImGui::PopStyleColor(); // Pop ImGuiCol_WindowBg

    // Calculate dimensions
    ImVec2 winSize = ImGui::GetWindowSize();
    const float button_size_y = winSize.y;
    const float button_size_x = button_size_y * 1.5f; // Wider buttons for better hit area
    const float total_buttons_width = button_size_x * 3.0f;
    const float buildButtonWidth = 120.0f;
    const float control_buttons_start_x = winSize.x - total_buttons_width;

    // --- 1. Right-side control buttons (Drawn First) ---
    // Position cursor for the first button (Minimize)
    ImGui::SetCursorPos(ImVec2(control_buttons_start_x, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    // Helper lambda for consistent button styling and size
    auto pushTransparentButton = [&](const char* label, const ImVec4& hoverColor, auto callback) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
        if (ImGui::Button(label, ImVec2(button_size_x, button_size_y))) callback();
        ImGui::PopStyleColor(3);
    };

    // MINIMIZE
    pushTransparentButton("_", ImVec4(0.3f,0.3f,0.3f,0.5f), [](){ glfwIconifyWindow(g_MainWindow); });
    ImGui::SameLine(0, 0); // No extra spacing between buttons

    // MAXIMIZE/RESTORE
    bool maximized = glfwGetWindowAttrib(g_MainWindow, GLFW_MAXIMIZED);
    pushTransparentButton(maximized ? "[]" : "O", ImVec4(0.3f,0.3f,0.3f,0.5f), [maximized](){
        if(maximized) glfwRestoreWindow(g_MainWindow); else glfwMaximizeWindow(g_MainWindow);
    });
    ImGui::SameLine(0, 0); // No extra spacing between buttons

    // CLOSE
    pushTransparentButton("X", ImVec4(0.9f,0.2f,0.2f,1.0f), [](){ glfwSetWindowShouldClose(g_MainWindow, GLFW_TRUE); });

    ImGui::PopStyleVar(); // Pop ImGuiStyleVar_FrameRounding

    // --- 2. Logo and Menu Bar (Left Side) ---
    // Reposition cursor to the left to draw the Logo/Menu on top of the title bar background
    ImGui::SetCursorPos(ImVec2(5.0f,0.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("⚛ Engine Editor");

    ImGui::SameLine(0, 15.0f);

    // Menu Bar
    if(ImGui::BeginMenuBar()) {
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, accent);
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Text]);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);

        if(ImGui::BeginMenu("File")) {
            ImGui::MenuItem("New Project");
            ImGui::MenuItem("Load Project");
            ImGui::Separator();
            if(ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(g_MainWindow, GLFW_TRUE);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Undo","CTRL+Z");
            ImGui::MenuItem("Redo","CTRL+Y",false,false);
            ImGui::Separator();
            ImGui::MenuItem("Cut","CTRL+X");
            ImGui::MenuItem("Copy","CTRL+C");
            ImGui::MenuItem("Paste","CTRL+V");
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Add")) {
            if(ImGui::MenuItem("New World")) Engine::Core::SaveWorld(EditorUi::projectDir.string() + "/Scenes/");
            ImGui::EndMenu();
        }

        ImGui::PopStyleColor(3); // Pop MenuBarBg, Header, HeaderHovered
        ImGui::EndMenuBar();
    }

    float menu_end_x = ImGui::GetCursorPosX();

    // --- 3. Centered Build Button ---
    // Calculate the start position for a centered button between menu_end_x and control_buttons_start_x
    float center_space_start = menu_end_x;
    float center_space_end = control_buttons_start_x;
    float build_start_x = center_space_start + ((center_space_end - center_space_start) / 2.0f) - (buildButtonWidth / 2.0f);

    // Ensure the button isn't drawn over the menu bar
    if (build_start_x < menu_end_x) build_start_x = menu_end_x;

    // Position and draw the button
    ImGui::SetCursorPos(ImVec2(build_start_x, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if(ImGui::Button("Build Project", ImVec2(buildButtonWidth, title_bar_height))) {
        EditorUi::BuildProject();
    }
    ImGui::PopStyleColor(3);

    float build_end_x = ImGui::GetCursorPosX();

    // --- 4. Drag Region (Invisible Buttons for Dragging, covers remaining empty space) ---

    // Left Drag Region (Space between Menu Bar and Build Button)
    ImGui::SetCursorPos(ImVec2(menu_end_x, 0));
    float drag_width_left = build_start_x - menu_end_x;

    if (drag_width_left > 0) {
        ImGui::InvisibleButton("##DragRegionLeft", ImVec2(drag_width_left, title_bar_height));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            int x, y;
            glfwGetWindowPos(g_MainWindow, &x, &y);
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            glfwSetWindowPos(g_MainWindow, x + (int)delta.x, y + (int)delta.y);
        }
    }

    // Right Drag Region (Space between Build Button and Control Buttons)
    ImGui::SetCursorPos(ImVec2(build_end_x, 0));
    float drag_width_right = control_buttons_start_x - build_end_x;

    if (drag_width_right > 0) {
        ImGui::InvisibleButton("##DragRegionRight", ImVec2(drag_width_right, title_bar_height));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            int x, y;
            glfwGetWindowPos(g_MainWindow, &x, &y);
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            glfwSetWindowPos(g_MainWindow, x + (int)delta.x, y + (int)delta.y);
        }
    }


    ImGui::End(); // ##CustomTitleBar
    ImGui::PopStyleVar(2); // Pop WindowPadding, WindowBorderSize
}

void EditorUi::DrawWindowUi() {
    DrawCustomTitleBar();

    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float title_bar_height = 35.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + title_bar_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - title_bar_height));
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking
                                        | ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoCollapse
                                        | ImGuiWindowFlags_NoResize
                                        | ImGuiWindowFlags_NoMove
                                        | ImGuiWindowFlags_NoBringToFrontOnFocus
                                        | ImGuiWindowFlags_NoNavFocus
                                        | ImGuiWindowFlags_NoScrollbar;

    // CRITICAL: Push 0 padding for the main dockspace host window to remove the app-edge margin
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpaceHost", nullptr, window_flags);

    ImGui::PopStyleVar(3); // Pop rounding, border, and padding

    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0,0), dockspace_flags);
    }

    LoggerUI::LoggerWindow();

    // Viewport Window - needs its own specific no-padding push/pop
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0,0});
    ImGui::Begin("Viewport");

    // --- FIX START: Use GetCursorScreenPos() for the true image origin ---
    // This gives the absolute screen position (x, y) where the FBO image starts drawing,
    // correctly accounting for the ImGui Tab Bar / internal window decorations.
    ImVec2 imageStartScreenPos = ImGui::GetCursorScreenPos();
    g_WindowPosCache["Viewport"] = imageStartScreenPos; // Cache the image's top-left screen position

    // Calculate mouse position relative to the image's top-left pixel
    const ImVec2 mouseScreenPos = ImGui::GetMousePos();
    viewportMousePos = {
        mouseScreenPos.x - imageStartScreenPos.x,
        mouseScreenPos.y - imageStartScreenPos.y
    };
    // --- FIX END ---


    // Use GetContentRegionAvail for the size, as it's the drawable area
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    g_WindowSizeCache["Viewport"] = contentSize; // Cache the size for consistency

    Engine::Renderer::SetViewportSize(contentSize.x, contentSize.y);

    if(uint32_t textureID = Engine::Renderer::GetRenderTextureID(); textureID != 0) {
        // ImVec2(0,1) to ImVec2(1,0) flips the image vertically for OpenGL FBOs
        ImGui::Image((ImTextureID)(intptr_t)textureID, contentSize, ImVec2(0,1), ImVec2(1,0));
    }

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::End();
}


void EditorUi::BuildProject() {
    Engine::ProjectManager::BuildProjectAsync();
}

Engine::Math::Vec2 EditorUi::GetMousePositionInWindow(const std::string& windowName) {
    const ImVec2 mouse = ImGui::GetMousePos();
    auto it = g_WindowPosCache.find(windowName);
    if(it == g_WindowPosCache.end()) return {-1,-1};
    return { mouse.x - it->second.x, mouse.y - it->second.y };
}