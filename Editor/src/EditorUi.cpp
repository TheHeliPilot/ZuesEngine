#include "../include/EditorUi.h"
#include "imgui.h"
#include <unordered_map>
#include "Engine.h"
#include "../include/LoggerUI.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>

#include "Core.h"
#include "imgui_internal.h"
#include "../include/AssetBrowserUI.h"
#include "../include/HierarchyUI.h"
#include "../include/InspectorUI.h"
#include "../include/TextureCutterUI.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

// Note: Ensure stbi_image is included/linked for LoadTextureFromFile to work.
extern "C" {
    extern void stbi_set_flip_vertically_on_load(int flag);
    extern unsigned char* stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp);
    extern void stbi_image_free(void *retval_from_stbi_load);
}

extern GLFWwindow* g_MainWindow;

using namespace EditorWindows;

std::filesystem::path EditorUi::projectDir = "../../MyGameProject";
std::vector<EntityID> EditorUi::selectedEntities = {};
Engine::Math::Vec2 EditorUi::viewportMousePos = {-1, -1};
Engine::Math::Vec2 EditorUi::viewportSize = {-1, -1};
static std::unordered_map<std::string, ImVec2> g_WindowPosCache;
static std::unordered_map<std::string, ImVec2> g_WindowSizeCache;

static ImVec2 FromImVec2(const ImVec2& v) { return {v.x, v.y}; }

// --- STATE FOR RESIZING ---
enum class ResizeEdge {
    NONE = 0,
    LEFT = 1,
    RIGHT = 2,
    TOP = 4,
    BOTTOM = 8,
    TOP_LEFT = LEFT | TOP,
    TOP_RIGHT = RIGHT | TOP,
    BOTTOM_LEFT = LEFT | BOTTOM,
    BOTTOM_RIGHT = RIGHT | BOTTOM
};

static ResizeEdge g_ActiveResizeEdge = ResizeEdge::NONE;
static double g_LastCursorX = 0.0;
static double g_LastCursorY = 0.0;
// --------------------------


// ========================================================================================
// LoadTextureFromFile - Restored (Kept as is from original content)
// ========================================================================================
uint32_t LoadTextureFromFile(const char* filepath) {
    uint32_t texture_id = 0;
    int w, h, channels;

    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(filepath, &w, &h, &channels, 4);

    if (!data) {
        std::cerr << "Failed to load image: " << filepath << std::endl;
        return 0;
    }

    // Flip image manually
    int rowSize = w * 4;
    unsigned char* tempRow = new unsigned char[rowSize];
    for (int y = 0; y < h / 2; y++) {
        unsigned char* rowA = data + y * rowSize;
        unsigned char* rowB = data + (h - 1 - y) * rowSize;
        memcpy(tempRow, rowA, rowSize);
        memcpy(rowA, rowB, rowSize);
        memcpy(rowB, tempRow, rowSize);
    }
    delete[] tempRow;

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    return texture_id;
}
// ========================================================================================
// DRAW CUSTOM TITLE BAR - MODIFIED FOR ABSOLUTE CENTERING AND FIXED LEFT CLAMP
// ========================================================================================
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
                                 | ImGuiWindowFlags_NoScrollbar;

    const ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_TitleBgActive];
    ImGui::PushStyleColor(ImGuiCol_WindowBg, accent);
    ImGui::Begin("##CustomTitleBar", nullptr, flags);
    ImGui::PopStyleColor(); // Pop ImGuiCol_WindowBg

    // Calculate dimensions
    ImVec2 winSize = ImGui::GetWindowSize();
    const float button_size_y = winSize.y;
    const float button_size_x = button_size_y * 1.5f;
    const float total_buttons_width = button_size_x * 3.0f;
    const float control_buttons_start_x = winSize.x - total_buttons_width;

    // --- 0. Icon Texture Loading ---
    static uint32_t logo_texture_id = 0;
    static uint32_t minimize_texture_id = 0;
    static uint32_t maximize_texture_id = 0;
    static uint32_t restore_texture_id = 0;
    static uint32_t close_texture_id = 0;
    static uint32_t build_texture_id = 0;
    static uint32_t play_texture_id = 0;

    if (logo_texture_id == 0)      logo_texture_id     = LoadTextureFromFile("icons/ZuesLogoNoBG.png");
    if (minimize_texture_id == 0)  minimize_texture_id = LoadTextureFromFile("icons/System/Bar_Bottom.png");
    if (maximize_texture_id == 0)  maximize_texture_id = LoadTextureFromFile("icons/System/Window.png");
    if (restore_texture_id == 0)   restore_texture_id  = LoadTextureFromFile("icons/System/Devices.png");
    if (close_texture_id == 0)     close_texture_id    = LoadTextureFromFile("icons/Menu/Close_LG.png");
    if (build_texture_id == 0)     build_texture_id    = LoadTextureFromFile("icons/File/Download_Package.png");
    if (play_texture_id == 0)      play_texture_id     = LoadTextureFromFile("icons/Media/Play.png");
    // -------------------------------

    // --- 1. Right-side control buttons (Window Controls) ---
    ImGui::SetCursorPos(ImVec2(control_buttons_start_x, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    // Custom lambda for the control buttons (Minimize, Maximize, Close)
    auto pushTransparentImageButton = [&](const char* id, uint32_t textureID, const ImVec2& size, const ImVec4& hoverColor, auto callback) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // Transparent background
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, hoverColor);

        // Use a fixed frame padding to center the icon in the window control block
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2( (button_size_x - size.x) / 2.0f, (button_size_y - size.y) / 2.0f ));

        if (ImGui::ImageButton(
            id, // Unique ID
            static_cast<ImTextureID>(static_cast<intptr_t>(textureID)),
            size,
            ImVec2(0, 0), ImVec2(1, 1),
            ImVec4(0,0,0,0) // bg_col (transparent)
        )) {
            callback();
        }

        ImGui::PopStyleVar(); // Pop FramePadding
        ImGui::PopStyleColor(3);
    };

    const float icon_size_xy = 16.0f; // A suitable size for window control icons
    const ImVec2 icon_size = {icon_size_xy, icon_size_xy};

    // Minimize Button (Using unique ID)
    pushTransparentImageButton("##Minimize", minimize_texture_id, icon_size, ImVec4(0.3f, 0.3f, 0.3f, 0.5f), [](){ glfwIconifyWindow(g_MainWindow); });
    ImGui::SameLine(0, 0);

    // Maximize/Restore Button (Using unique ID)
    bool maximized = glfwGetWindowAttrib(g_MainWindow, GLFW_MAXIMIZED);
    uint32_t current_maximize_id = maximized ? restore_texture_id : maximize_texture_id;
    pushTransparentImageButton("##Maximize", current_maximize_id, icon_size, ImVec4(0.3f, 0.3f, 0.3f, 0.5f), [maximized](){
        if(maximized) glfwRestoreWindow(g_MainWindow); else glfwMaximizeWindow(g_MainWindow);
    });
    ImGui::SameLine(0, 0);

    // Close Button (Using unique ID)
    const float close_icon_size_xy = 14.0f;
    const ImVec2 close_icon_size = {close_icon_size_xy, close_icon_size_xy};
    pushTransparentImageButton("##Close", close_texture_id, close_icon_size, ImVec4(0.9f, 0.2f, 0.2f, 1.0f), [](){ glfwSetWindowShouldClose(g_MainWindow, GLFW_TRUE); });

    ImGui::PopStyleVar(); // Pop FrameRounding
    // -------------------------------------------------------------------------------------

    // --- 2. Logo, Engine Name, and FPS (Left Side) ---
    const float logo_margin_y = 5.0f;
    const float logo_size = title_bar_height - (logo_margin_y * 2.0f);

    ImGui::SetCursorPos(ImVec2(5.0f, logo_margin_y));

    // Draw the Logo
    if (logo_texture_id != 0) {
        ImGui::Image((ImTextureID)(intptr_t)logo_texture_id, ImVec2(logo_size, logo_size), ImVec2(0,1), ImVec2(1,0));
    }

    // Position the text next to the logo
    ImGui::SameLine();
    ImGui::SetCursorPosY(logo_margin_y);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Zues Editor");

    // --- FPS DISPLAY (Fixed Width, Jitter Fix) ---
    ImGui::SameLine(0, 5.0f);
    ImGui::SetWindowFontScale(0.8f);

    // FIX 1: Fix Jittering by reserving a fixed width and right-aligning the text manually.
    const float fixed_fps_width = ImGui::CalcTextSize("999").x + 10.0f; // Width for "999" plus margin
    const float horizontal_padding_after_fps = 10.0f; // Margin before Menu Bar

    float fps = ImGui::GetIO().Framerate;
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "%.0f", fps);
    ImVec2 actual_fps_size = ImGui::CalcTextSize(fps_text);

    // Reserve the fixed width space for the FPS number
    ImGui::InvisibleButton("##FPS_Region", ImVec2(fixed_fps_width, title_bar_height));
    ImVec2 region_min = ImGui::GetItemRectMin();
    ImVec2 region_max = ImGui::GetItemRectMax();

    // Draw the text manually, right-aligned within the reserved space, and vertically centered
    if (ImGui::IsItemVisible()) {
        float text_start_y = region_min.y + (title_bar_height - actual_fps_size.y) / 2.0f;
        float text_start_x = region_max.x - actual_fps_size.x; // Right-aligned to the region max X

        ImGui::GetWindowDrawList()->AddText(
            ImVec2(text_start_x, text_start_y),
            ImGui::GetColorU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)),
            fps_text
        );
    }

    ImGui::SetWindowFontScale(1.0f);
    // --- END FPS DISPLAY ---

    // 8. Manually position the cursor for the Menu Bar
    // The menu bar starts after the reserved FPS region plus margin.
    float menu_bar_start_x = region_max.x + horizontal_padding_after_fps;
    ImGui::SetCursorPosX(menu_bar_start_x);

    // Menu Bar
    ImGui::SameLine();
    if (ImGui::Button("File")) {
        ImVec2 pos = ImGui::GetItemRectMin();
        pos.y = ImGui::GetItemRectMax().y;
        ImGui::SetNextWindowPos(pos);
        ImGui::OpenPopup("file_popup");
    }

    if (ImGui::BeginPopup("file_popup")) {
        if (ImGui::Selectable("New Project")) { /* action */ }
        if (ImGui::Selectable("Load World"))   { /* action */ }
        if (ImGui::Selectable("Save World"))  {  }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Edit")) {
        ImVec2 pos = ImGui::GetItemRectMin();
        pos.y = ImGui::GetItemRectMax().y;
        ImGui::SetNextWindowPos(pos);
        ImGui::OpenPopup("edit_popup");
    }

    if (ImGui::BeginPopup("edit_popup")) {
        if (ImGui::Selectable("Undo")) { /* action */ }
        if (ImGui::Selectable("Redo"))   { /* action */ }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("New")) {
        ImVec2 pos = ImGui::GetItemRectMin();
        pos.y = ImGui::GetItemRectMax().y;
        ImGui::SetNextWindowPos(pos);
        ImGui::OpenPopup("new_popup");
    }

    if (ImGui::BeginPopup("new_popup")) {
        if (ImGui::Selectable("New Component")) { /* action */ }
        if (ImGui::Selectable("New Item"))   { /* action */ }
        if (ImGui::Selectable("New World"))   { /* action */ }
        ImGui::EndPopup();
    }

    // --- 3. Build and Play Buttons Control (Image + Text) ---

    // Define fixed widths for the two buttons
    constexpr float build_button_width = 100.0f;
    constexpr float play_button_width = 90.0f;
    constexpr float icon_text_spacing = 5.0f; // Spacing between icon and text
    constexpr float control_spacing = 15.0f;
    const float buildControlTotalWidth = build_button_width + play_button_width + control_spacing;

    // Margin from Window Controls
    constexpr float horizontal_margin = 10.0f;

    // --- FIX V3: Absolute Centering with Fixed Left Clamp (Stable) ---
    // This calculation relies ONLY on the window's total width (winSize.x) and fixed constants,
    // guaranteeing stability against window movement or ImGui cursor jitter.

    // 1. Calculate the ideal start X to center the block on the entire window width. (Stable)
    float ideal_center_x = (winSize.x - buildControlTotalWidth) / 2.0f;

    // 2. Define the absolute minimum start X for the buttons using a generous, fixed offset.
    // This value is stable and prevents overlap with the Menu Bar (even if the menu bar's width calculation jitters).
    constexpr float ABSOLUTE_MINIMUM_START_X = 450.0f;

    // 3. Clamp the ideal center position: use the centered position UNLESS it overlaps the fixed minimum start point.
    float build_start_x = std::max(ideal_center_x, ABSOLUTE_MINIMUM_START_X);

    // 4. Also clamp against the right window controls (control_buttons_start_x is stable).
    float maximum_safe_start_x = control_buttons_start_x - buildControlTotalWidth - horizontal_margin;
    build_start_x = std::min(build_start_x, maximum_safe_start_x);

    // --- END STABLE CENTERING LOGIC ---

    // Position the entire control block
    ImGui::SetCursorPos(ImVec2(build_start_x, 0));

    // Common style for the control buttons
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(control_spacing, 0.0f));

    const float text_height = ImGui::CalcTextSize("Play").y;

    const float icon_size_xy_controls = 16.0f; // Icon size used for Build/Play
    const float icon_y_start = (title_bar_height - icon_size_xy_controls) / 2.0f;
    const float text_y_start = (title_bar_height - text_height) / 2.0f;

    // --- Build Button ---
    // Calculate content width for horizontal centering inside the button
    const float build_text_width = ImGui::CalcTextSize("Build").x;
    const float build_content_total_width = icon_size_xy_controls + icon_text_spacing + build_text_width;
    const float build_content_start_x_offset = (build_button_width - build_content_total_width) / 2.0f;

    // FramePadding here is used vertically to reserve the space required by the icon,
    // and horizontally to push the content to the center.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(build_content_start_x_offset, icon_y_start));
    if(ImGui::Button("##BuildButton", ImVec2(build_button_width, title_bar_height))) {
        EditorUi::BuildProject(false); // Build Only
    }
    ImGui::PopStyleVar(); // Pop FramePadding

    if (ImGui::IsItemVisible()) {
        ImVec2 button_min = ImGui::GetItemRectMin();

        // Icon Draw - Y aligned using icon_y_start
        ImGui::SetCursorScreenPos(ImVec2(button_min.x + build_content_start_x_offset, button_min.y + icon_y_start));
        ImGui::Image((ImTextureID)(intptr_t)build_texture_id, ImVec2(icon_size_xy_controls, icon_size_xy_controls), ImVec2(0, 0), ImVec2(1, 1));

        // Text Draw - Y aligned using text_y_start
        ImGui::SetCursorScreenPos(ImVec2(
            button_min.x + build_content_start_x_offset + icon_size_xy_controls + icon_text_spacing,
            button_min.y + text_y_start
        ));
        ImGui::TextUnformatted("Build");
    }

    ImGui::SameLine(); // Move cursor for the next button (will use control_spacing from ItemSpacing)
    ImGui::SetCursorPosY(0.0f);


    // --- Play Button ---
    // Calculate content width for horizontal centering inside the button
    const float play_text_width = ImGui::CalcTextSize("Play").x;
    const float play_content_total_width = icon_size_xy_controls + icon_text_spacing + play_text_width;
    const float play_content_start_x_offset = (play_button_width - play_content_total_width) / 2.0f;

    // Use FramePadding to push the content to the center
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(play_content_start_x_offset, icon_y_start));
    if(ImGui::Button("##PlayButton", ImVec2(play_button_width, title_bar_height))) {
        EditorUi::BuildProject(true); // Build and Play
    }
    ImGui::PopStyleVar(); // Pop FramePadding

    if (ImGui::IsItemVisible()) {
        ImVec2 button_min = ImGui::GetItemRectMin();

        // Icon Draw - Y aligned using icon_y_start
        ImGui::SetCursorScreenPos(ImVec2(button_min.x + play_content_start_x_offset, button_min.y + icon_y_start));
        ImGui::Image((ImTextureID)(intptr_t)play_texture_id, ImVec2(icon_size_xy_controls, icon_size_xy_controls), ImVec2(0, 0), ImVec2(1, 1));

        // Text Draw - Y aligned using text_y_start
        ImGui::SetCursorScreenPos(ImVec2(
            button_min.x + play_content_start_x_offset + icon_size_xy_controls + icon_text_spacing,
            button_min.y + text_y_start
        ));
        ImGui::TextUnformatted("Play");
    }

    ImGui::PopStyleVar(2); // Pop ItemSpacing and FrameRounding

    // We now fetch the cursor position after the button block for the drag region.
    // This value is relative to the stable button position, so it should be correct.
    float build_end_x = ImGui::GetCursorPosX();

    // --- 4. Drag Region (Unchanged) ---
    ImGui::SetCursorPos(ImVec2(build_end_x, 0));
    float drag_width_right = control_buttons_start_x - build_end_x;

    if (drag_width_right > 0) {
        ImGui::InvisibleButton("##DragRegionRight", ImVec2(drag_width_right, title_bar_height));
        if (g_ActiveResizeEdge == ResizeEdge::NONE && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            int x, y;
            glfwGetWindowPos(g_MainWindow, &x, &y);
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            glfwSetWindowPos(g_MainWindow, x + (int)delta.x, y + (int)delta.y);
        }
    }

    ImGui::End(); // ##CustomTitleBar
    ImGui::PopStyleVar(2); // Pop WindowPadding, WindowBorderSize
}


// ... (UNMODIFIED FUNCTIONS) ...
void EditorUi::HandleWindowResize() {
    if (glfwGetWindowAttrib(g_MainWindow, GLFW_MAXIMIZED)) {
        glfwSetCursor(g_MainWindow, glfwCreateStandardCursor(GLFW_ARROW_CURSOR));
        g_ActiveResizeEdge = ResizeEdge::NONE;
        return;
    }

    if (g_ActiveResizeEdge == ResizeEdge::NONE && ImGui::IsAnyItemActive()) {
        glfwSetCursor(g_MainWindow, glfwCreateStandardCursor(GLFW_ARROW_CURSOR));
        return;
    }

    constexpr float BORDER_SIZE = 8.0f;
    constexpr float TITLE_BAR_HEIGHT = 35.0f;

    double client_x, client_y;
    glfwGetCursorPos(g_MainWindow, &client_x, &client_y);
    const ImVec2 mousePos = {(float)client_x, (float)client_y};

    int win_x, win_y, win_w, win_h;
    glfwGetWindowPos(g_MainWindow, &win_x, &win_y);
    glfwGetWindowSize(g_MainWindow, &win_w, &win_h);

    bool left_mouse_down = glfwGetMouseButton(g_MainWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    // --- State 1: Detect Resize Start or Update Cursor ---
    if (g_ActiveResizeEdge == ResizeEdge::NONE) {
        bool near_left = mousePos.x >= 0 && mousePos.x <= BORDER_SIZE;
        bool near_right = mousePos.x <= win_w && mousePos.x >= win_w - BORDER_SIZE;
        bool near_bottom = mousePos.y <= win_h && mousePos.y >= win_h - BORDER_SIZE;
        bool near_top = mousePos.y >= 0 && mousePos.y <= BORDER_SIZE;

        bool over_title_bar_content = mousePos.y > BORDER_SIZE && mousePos.y < TITLE_BAR_HEIGHT && !near_left && !near_right;

        ResizeEdge potentialEdge = ResizeEdge::NONE;
        int cursorShape = GLFW_ARROW_CURSOR;

        if (near_left && near_top) { potentialEdge = ResizeEdge::TOP_LEFT; cursorShape = GLFW_RESIZE_NWSE_CURSOR; }
        else if (near_right && near_bottom) { potentialEdge = ResizeEdge::BOTTOM_RIGHT; cursorShape = GLFW_RESIZE_NWSE_CURSOR; }
        else if (near_right && near_top) { potentialEdge = ResizeEdge::TOP_RIGHT; cursorShape = GLFW_RESIZE_NESW_CURSOR; }
        else if (near_left && near_bottom) { potentialEdge = ResizeEdge::BOTTOM_LEFT; cursorShape = GLFW_RESIZE_NESW_CURSOR; }
        else if (near_left || near_right) { potentialEdge = near_left ? ResizeEdge::LEFT : ResizeEdge::RIGHT; cursorShape = GLFW_HRESIZE_CURSOR; }
        else if (near_top || near_bottom) { potentialEdge = near_top ? ResizeEdge::TOP : ResizeEdge::BOTTOM; cursorShape = GLFW_VRESIZE_CURSOR; }

        if (potentialEdge != ResizeEdge::NONE && !over_title_bar_content) {
            glfwSetCursor(g_MainWindow, glfwCreateStandardCursor(cursorShape));
            if (left_mouse_down) {
                g_ActiveResizeEdge = potentialEdge;
                glfwGetCursorPos(g_MainWindow, &g_LastCursorX, &g_LastCursorY);
            }
        } else {
            glfwSetCursor(g_MainWindow, glfwCreateStandardCursor(GLFW_ARROW_CURSOR));
        }
    }

    // --- State 2: Active Resize (Dragging) ---
    if (g_ActiveResizeEdge != ResizeEdge::NONE) {
        if (!left_mouse_down) {
            g_ActiveResizeEdge = ResizeEdge::NONE;
            return;
        }

        double current_x, current_y;
        glfwGetCursorPos(g_MainWindow, &current_x, &current_y);
        const int delta_x = (int)(current_x - g_LastCursorX);
        const int delta_y = (int)(current_y - g_LastCursorY);

        int new_x = win_x, new_y = win_y, new_w = win_w, new_h = win_h;
        const int MIN_W = 500, MIN_H = 300;

        if ((int)g_ActiveResizeEdge & (int)ResizeEdge::LEFT) {
            new_w = win_w - delta_x;
            if (new_w > MIN_W) {
                new_x = win_x + delta_x;
            } else {
                new_x = win_x + (win_w - MIN_W);
                new_w = MIN_W;
            }
        }

        if ((int)g_ActiveResizeEdge & (int)ResizeEdge::RIGHT) {
            new_w = std::max(MIN_W, win_w + delta_x);
        }

        if ((int)g_ActiveResizeEdge & (int)ResizeEdge::TOP) {
            new_h = win_h - delta_y;
            if (new_h > MIN_H) {
                new_y = win_y + delta_y;
            } else {
                new_y = win_y + (win_h - MIN_H);
                new_h = MIN_H;
            }
        }

        if ((int)g_ActiveResizeEdge & (int)ResizeEdge::BOTTOM) {
            new_h = std::max(MIN_H, win_h + delta_y);
        }

        if (new_x != win_x || new_y != win_y || new_w != win_w || new_h != win_h) {
            glfwSetWindowPos(g_MainWindow, new_x, new_y);
            glfwSetWindowSize(g_MainWindow, new_w, new_h);
        }

        glfwGetCursorPos(g_MainWindow, &g_LastCursorX, &g_LastCursorY);
    }
}


void EditorUi::DrawWindowUi() {
    DrawCustomTitleBar();

    HandleWindowResize();

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
                                        | ImGuiWindowFlags_NoNavFocus
                                        | ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpaceHost", nullptr, window_flags);

    ImGui::PopStyleVar(3);

    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0,0), dockspace_flags);
    }

    LoggerUI::LoggerWindow();
    HierarchyUI::HierarchyWindow();
    InspectorUI::InspectorWindow();
    AssetBrowserUI::AssetBrowserWindow();
    TextureCutterUI::TextureCutterWindow();
    ImGui::ShowDemoWindow();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0,0});
    ImGui::Begin("Viewport");

    ImVec2 imageStartScreenPos = ImGui::GetCursorScreenPos();
    g_WindowPosCache["Viewport"] = imageStartScreenPos;

    const ImVec2 mouseScreenPos = ImGui::GetMousePos();
    viewportMousePos = {
        mouseScreenPos.x - imageStartScreenPos.x,
        mouseScreenPos.y - imageStartScreenPos.y
    };

    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    g_WindowSizeCache["Viewport"] = contentSize;

    Engine::Renderer::SetViewportSize(contentSize.x, contentSize.y);

    if(uint32_t textureID = Engine::Renderer::GetRenderTextureID(); textureID != 0) {
        ImGui::Image((ImTextureID)(intptr_t)textureID, contentSize, ImVec2(0,1), ImVec2(1,0));
    }

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::End();
}


void EditorUi::BuildProject(bool play) {
    Engine::ProjectManager::BuildProjectAsync(play);
}

Engine::Math::Vec2 EditorUi::GetMousePositionInWindow(const std::string& windowName) {
    const ImVec2 mouse = ImGui::GetMousePos();
    const auto it = g_WindowPosCache.find(windowName);
    if(it == g_WindowPosCache.end()) return {-1,-1};
    return { mouse.x - it->second.x, mouse.y - it->second.y };
}

bool EditorUi::IsEntitySelected(const EntityID &entityID)
{
    for (const auto& selectedEntity : selectedEntities)
    {
        if (selectedEntity == entityID) return true;
    }

    return false;
}
