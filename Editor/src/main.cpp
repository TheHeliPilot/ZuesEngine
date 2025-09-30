#include <filesystem>
#include "Engine.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../include/EditorUi.h"
#include "Input.h"
#include "Renderer.h"
#include "../include/LoggerUI.h"

using namespace EditorWindows;

// Global pointer for Input handling
GLFWwindow* g_MainWindow = nullptr;

// Helper function to convert a hex string to an ImVec4 color
static ImVec4 HexToImVec4(const char* hex) {
    int r, g, b;
    if (sscanf(hex, "#%02x%02x%02x", &r, &g, &b) != 3) {
        return ImVec4(1.0f, 0.0f, 1.0f, 1.0f); // Fallback to magenta
    }
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

void ApplyDarkMinimalTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // --- 1. Geometry and Spacing (Modern and Compact) ---
    style.Alpha             = 1.0f;           // Full opacity
    style.WindowPadding     = ImVec2(8, 8);   // Default
    style.WindowRounding    = 2.0f;           // Slightly rounded corners
    style.FramePadding      = ImVec2(8, 4);   // Increased horizontal padding for fields/buttons
    style.FrameRounding     = 2.0f;           // Slightly rounded corners for frames
    style.ItemSpacing       = ImVec2(8, 4);   // Compact vertical spacing
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 20.0f;
    style.ScrollbarSize     = 10.0f;          // Thinner scrollbar
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;           // Slight rounding for tabs
    style.ChildRounding     = 2.0f;
    style.PopupRounding     = 2.0f;

    // --- 2. Title and Border (Minimalist) ---
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f); // Centered title bar text
    style.WindowBorderSize  = 0.0f;           // Eliminate window borders
    style.FrameBorderSize   = 0.0f;           // Eliminate frame borders
    style.PopupBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;           // Eliminate tab borders

    // --- 3. Colors ---
    // (Keep the original palette, but refine usage for a modern look)
    const ImVec4 bg          = HexToImVec4("#0A0A0A");
    const ImVec4 bg_panel    = HexToImVec4("#141414");
    const ImVec4 accent      = HexToImVec4("#1C1C1C");
    const ImVec4 hover       = HexToImVec4("#252525");
    const ImVec4 text        = HexToImVec4("#EDEDED");
    const ImVec4 text_disabled = HexToImVec4("#7A7A7A");
    const ImVec4 border      = HexToImVec4("#1F1F1F");
    const ImVec4 success     = HexToImVec4("#2F442F");
    const ImVec4 warning     = HexToImVec4("#3F382B");
    const ImVec4 error       = HexToImVec4("#3D2F2F");

    ImVec4* colors = style.Colors;

    // Windows / panels
    colors[ImGuiCol_WindowBg]         = bg_panel;
    colors[ImGuiCol_ChildBg]          = bg_panel;
    colors[ImGuiCol_PopupBg]          = bg_panel;
    colors[ImGuiCol_Border]           = border;
    colors[ImGuiCol_BorderShadow]     = bg;

    // Text
    colors[ImGuiCol_Text]             = text;
    colors[ImGuiCol_TextDisabled]     = text_disabled;

    // Title bars (Keep as is, used by the custom title bar accent)
    colors[ImGuiCol_TitleBg]          = accent;
    colors[ImGuiCol_TitleBgActive]    = hover;
    colors[ImGuiCol_TitleBgCollapsed] = accent;

    // Frames (e.g., input fields, checkboxes) - Use a darker accent for a subtle depth
    colors[ImGuiCol_FrameBg]          = accent; // Darker accent for a more 'contained' look
    colors[ImGuiCol_FrameBgHovered]   = hover;
    colors[ImGuiCol_FrameBgActive]    = border; // Slightly lighter when active

    // Buttons - Use the border color for a more subtle look when not hovered
    colors[ImGuiCol_Button]           = border; // Subtle button background
    colors[ImGuiCol_ButtonHovered]    = hover;
    colors[ImGuiCol_ButtonActive]     = accent;

    // Headers (e.g., CollapsingHeader, Menu Bar)
    colors[ImGuiCol_Header]           = bg_panel; // Use panel background for a flat look
    colors[ImGuiCol_HeaderHovered]    = hover;
    colors[ImGuiCol_HeaderActive]     = accent;

    colors[ImGuiCol_CheckMark]        = text;
    colors[ImGuiCol_SliderGrab]       = hover;
    colors[ImGuiCol_SliderGrabActive] = text; // Lighter grab for contrast

    // Tabs (The key area for a modern look)
    colors[ImGuiCol_Tab]               = bg_panel; // Tab is same as window background when unfocused
    colors[ImGuiCol_TabHovered]        = hover;
    colors[ImGuiCol_TabActive]         = border; // Subtle background for active tab
    colors[ImGuiCol_TabUnfocused]      = bg_panel;
    colors[ImGuiCol_TabUnfocusedActive] = border;

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg]        = bg;
    colors[ImGuiCol_ScrollbarGrab]      = border;
    colors[ImGuiCol_ScrollbarGrabHovered] = accent;
    colors[ImGuiCol_ScrollbarGrabActive]  = hover;

    // Resize grips
    colors[ImGuiCol_ResizeGrip]          = border;
    colors[ImGuiCol_ResizeGripHovered]   = accent;
    colors[ImGuiCol_ResizeGripActive]    = hover;

    // Separators
    colors[ImGuiCol_Separator]        = border;
    colors[ImGuiCol_SeparatorHovered] = hover;
    colors[ImGuiCol_SeparatorActive]  = accent;

    // Plots / graphs
    colors[ImGuiCol_PlotLines]           = text;
    colors[ImGuiCol_PlotLinesHovered]    = hover;
    colors[ImGuiCol_PlotHistogram]       = warning;
    colors[ImGuiCol_PlotHistogramHovered] = error;

    // Docking
    colors[ImGuiCol_DockingPreview]   = HexToImVec4("#5C5C5C"); // Slightly lighter grey for the dock target
    colors[ImGuiCol_DockingEmptyBg]   = bg;

    colors[ImGuiCol_TabSelectedOverline] = success; // or HexToImVec4("#2F442F")

    // --- 4. Special Flags for Modern UI ---
    // This is how you remove the arrows next to tab names!
    style.WindowMenuButtonPosition = ImGuiDir_None; // Removes the small 'triple-bar' menu button in the corner of docked windows

    // Optional: if using multiple viewports
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void DrawEditionStuff() {
    // --- 1. Define Selection State (SIMULATION) ---
    // In your real code, these would be member variables or properties of the drawn objects
    static bool isArrow1Selected = false;
    static bool isArrow2Selected = false;
    static bool isCircleSelected = false;
    static bool isMoveSquareSelected = false;

    // --- 2. Define Color Pairs (Using the suggested contrast colors) ---

    // Green (Success/Info)
    const ImVec4 GREEN_UNSELECTED = HexToImVec4("#2F442F");
    const ImVec4 GREEN_SELECTED   = HexToImVec4("#558C55");

    // Red (Error/Warning)
    const ImVec4 RED_UNSELECTED   = HexToImVec4("#3D2F2F");
    const ImVec4 RED_SELECTED     = HexToImVec4("#8C5555");

    // Blue (Accent/Primary)
    const ImVec4 BLUE_UNSELECTED  = HexToImVec4("#252833");
    const ImVec4 BLUE_SELECTED    = HexToImVec4("#55628C");

    Engine::Vec2 mousePos = Engine::Renderer::ScreenToWorld(EditorUi::GetMousePositionInWindow("Viewport"));

    ImVec4 s1 = isMoveSquareSelected ? BLUE_SELECTED : BLUE_UNSELECTED;
    Engine::Vec4 color4 = {s1.x, s1.y, s1.z, s1.w};
    Engine::Rect r1 = Engine::Renderer::DrawRect({.5f,.5f}, {1,1}, color4, 0);
    isMoveSquareSelected = Engine::HitTest::Rect(mousePos, r1);

    // Arrow 1 (Green/Z-Axis)
    // Select color: If selected, use GREEN_SELECTED, otherwise GREEN_UNSELECTED.
    ImVec4 c1 = isArrow1Selected ? GREEN_SELECTED : GREEN_UNSELECTED;
    Engine::Vec4 color1 = {c1.x, c1.y, c1.z, c1.w};
    Engine::Arrow a1 = Engine::Renderer::DrawArrow({0,0}, {0,3}, color1, .05f);
    isArrow1Selected = isMoveSquareSelected || Engine::HitTest::Arrow(mousePos, a1, .2f);

    // Arrow 2 (Red/X-Axis)
    ImVec4 c2 = isArrow2Selected ? RED_SELECTED : RED_UNSELECTED;
    Engine::Vec4 color2 = {c2.x, c2.y, c2.z, c2.w};
    Engine::Arrow a2 = Engine::Renderer::DrawArrow({0,0}, {3,0}, color2, .05f);
    isArrow2Selected = isMoveSquareSelected || Engine::HitTest::Arrow(mousePos, a2, .2f);

    // Circle (Blue)
    ImVec4 c3 = isCircleSelected ? BLUE_SELECTED : BLUE_UNSELECTED;
    Engine::Vec4 color3 = {c3.x, c3.y, c3.z, c3.w};
    Engine::Circle c = Engine::Renderer::DrawCircle({0,0}, 3, color3, 50, true, .1f);
    isCircleSelected = Engine::HitTest::Circle(mousePos, c, .2f);

}

int main() {
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW.");
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // custom bar

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Editor", nullptr, nullptr);
    if (!window) {
        LOG_ERROR("Failed to create GLFW window.");
        glfwTerminate();
        return -1;
    }
    g_MainWindow = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG_ERROR("Failed to initialize GLAD.");
        return -1;
    }

    Engine::Initialize(Engine::Network::Role::Host, "0.0.0.0", 7777);
    Engine::IEventSystem->Subscribe<Engine::LogEvent>(LoggerUI::GetLogEvent);

    if (!Engine::ProjectManager::OpenOrCreate(EditorUi::projectDir)) {
        LOG_ERROR("Failed to open or create project at " + EditorUi::projectDir.string());
        return -1;
    }

    const uint32_t textureID = Engine::Renderer::LoadTexture("assets/bucka.png");
    LOG_INFO("Loaded texture with ID: " + std::to_string(textureID));

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImFont* exo2Font = io.Fonts->AddFontFromFileTTF("fonts/Exo2-VariableFont_wght.ttf", 18.0f);
    if (!exo2Font) LOG_ERROR("Failed to load Exo2 font!");

    ApplyDarkMinimalTheme();
    //ImGui::StyleColorsClassic();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    LOG_INFO("Editor started successfully.");

    int display_w, display_h;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        Engine::Input::UpdateState();

        Engine::Renderer::Render();
        Engine::Update(System::SystemRole::Editor);

        DrawEditionStuff();

        Engine::Renderer::EndBatch();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        EditorUi::DrawWindowUi();

        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.07f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_context);
        }

        glfwSwapBuffers(window);
    }

    Engine::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    LOG_INFO("Editor exited cleanly.");
    return 0;
}
