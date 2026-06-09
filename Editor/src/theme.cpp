#include "editor.h"

#include <imgui.h>

#include <cstdint>
#include <cstdio>

namespace Engine::editor {

namespace {
    // Parse a "#RRGGBB" / "#RRGGBBAA" hex color into ImVec4 (linear 0..1).
    ImVec4 hex_to_imvec4(const char* hex) {
        if (!hex) return ImVec4{1, 0, 1, 1};
        if (hex[0] == '#') ++hex;

        unsigned int r = 0, g = 0, b = 0, a = 0xFF;
        const auto len = std::strlen(hex);
        if (len == 6) {
            std::sscanf(hex, "%02x%02x%02x", &r, &g, &b);
        } else if (len == 8) {
            std::sscanf(hex, "%02x%02x%02x%02x", &r, &g, &b, &a);
        }
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }
}

void theme_apply_dark_minimal() {
    ImGuiStyle& style = ImGui::GetStyle();

    // ---- 1. Geometry and spacing -------------------------------------------
    // Minimal pass: zero rounding everywhere (clean rectangles read as
    // simpler than soft pills), tighter item spacing so the editor
    // chrome doesn't crowd the body, slightly more breathing room
    // around tab labels.
    style.Alpha             = 1.0f;
    style.WindowPadding     = ImVec2(8, 8);
    style.WindowRounding    = 0.0f;
    style.FramePadding      = ImVec2(8, 4);
    style.FrameRounding     = 0.0f;
    style.ItemSpacing       = ImVec2(8, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 20.0f;
    style.ScrollbarSize     = 10.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;
    style.ChildRounding     = 0.0f;
    style.PopupRounding     = 0.0f;
    // A bit more horizontal padding inside tab labels — gives names
    // room to breathe so adjacent tabs read as distinct without needing
    // a separator line between them.
    style.FramePadding.y    = 5.0f;

    // ---- 2. Title and border ------------------------------------------------
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;
    // Tabs lean on a thin top-of-active overline (set via
    // TabSelectedOverline below) to indicate selection, plus a bg
    // contrast against the docked area. No bottom-border line.

    // ---- 3. Colors ----------------------------------------------------------
    const ImVec4 bg            = hex_to_imvec4("#0A0A0A");
    const ImVec4 bg_panel      = hex_to_imvec4("#141414");
    const ImVec4 accent        = hex_to_imvec4("#1C1C1C");
    const ImVec4 hover         = hex_to_imvec4("#252525");
    const ImVec4 text          = hex_to_imvec4("#EDEDED");
    const ImVec4 text_disabled = hex_to_imvec4("#7A7A7A");
    const ImVec4 border        = hex_to_imvec4("#1F1F1F");
    const ImVec4 success       = hex_to_imvec4("#2F442F");
    const ImVec4 warning       = hex_to_imvec4("#3F382B");
    const ImVec4 error_col     = hex_to_imvec4("#3D2F2F");

    ImVec4* c = style.Colors;

    c[ImGuiCol_WindowBg]            = bg_panel;
    c[ImGuiCol_ChildBg]             = bg_panel;
    c[ImGuiCol_PopupBg]             = bg_panel;
    c[ImGuiCol_Border]              = border;
    c[ImGuiCol_BorderShadow]        = bg;

    c[ImGuiCol_Text]                = text;
    c[ImGuiCol_TextDisabled]        = text_disabled;

    c[ImGuiCol_TitleBg]             = accent;
    c[ImGuiCol_TitleBgActive]       = hover;
    c[ImGuiCol_TitleBgCollapsed]    = accent;

    c[ImGuiCol_FrameBg]             = accent;
    c[ImGuiCol_FrameBgHovered]      = hover;
    c[ImGuiCol_FrameBgActive]       = border;

    c[ImGuiCol_Button]              = border;
    c[ImGuiCol_ButtonHovered]       = hover;
    c[ImGuiCol_ButtonActive]        = accent;

    c[ImGuiCol_Header]              = bg_panel;
    c[ImGuiCol_HeaderHovered]       = hover;
    c[ImGuiCol_HeaderActive]        = accent;

    c[ImGuiCol_CheckMark]           = text;
    c[ImGuiCol_SliderGrab]          = hover;
    c[ImGuiCol_SliderGrabActive]    = text;

    // Tab strip: inactive tabs blend with the panel background; the
    // active tab gets a subtly lighter fill so the eye lands on it
    // without needing a green stripe above. Hover is the same lighter
    // fill the rest of the UI uses.
    const ImVec4 tab_active = hex_to_imvec4("#1E1E1E");
    c[ImGuiCol_Tab]                 = bg_panel;
    c[ImGuiCol_TabHovered]          = hover;
    c[ImGuiCol_TabActive]           = tab_active;
    c[ImGuiCol_TabUnfocused]        = bg_panel;
    c[ImGuiCol_TabUnfocusedActive]  = tab_active;

    c[ImGuiCol_ScrollbarBg]         = bg;
    c[ImGuiCol_ScrollbarGrab]       = border;
    c[ImGuiCol_ScrollbarGrabHovered] = accent;
    c[ImGuiCol_ScrollbarGrabActive]  = hover;

    c[ImGuiCol_ResizeGrip]          = border;
    c[ImGuiCol_ResizeGripHovered]   = accent;
    c[ImGuiCol_ResizeGripActive]    = hover;

    // Separators: dimmer than panel border so the few that remain
    // aren't loud. The minimal pass removed most of them from the
    // editor chrome, but they still appear inside menus and modals.
    const ImVec4 sep_quiet(0.18f, 0.18f, 0.18f, 0.7f);
    c[ImGuiCol_Separator]           = sep_quiet;
    c[ImGuiCol_SeparatorHovered]    = hover;
    c[ImGuiCol_SeparatorActive]     = accent;

    c[ImGuiCol_PlotLines]           = text;
    c[ImGuiCol_PlotLinesHovered]    = hover;
    c[ImGuiCol_PlotHistogram]       = warning;
    c[ImGuiCol_PlotHistogramHovered] = error_col;

    c[ImGuiCol_DockingPreview]      = hex_to_imvec4("#5C5C5C");
    c[ImGuiCol_DockingEmptyBg]      = bg;

    // Active-tab top-of-tab line. The default would draw it bright;
    // we want a thin neutral accent that just hints at "this is the
    // current tab" without competing with content. Slightly brighter
    // than the active fill so it reads as a 1px highlight, not a band.
    c[ImGuiCol_TabSelectedOverline] = hex_to_imvec4("#3A3A3A");
    c[ImGuiCol_TabSelectedOverline].w = 0.0f;   // off (the bg contrast is enough)

    // ---- 4. Modern UI flags -------------------------------------------------
    style.WindowMenuButtonPosition  = ImGuiDir_None;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding         = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

}  // namespace Engine::editor
