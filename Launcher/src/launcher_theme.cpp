#include "launcher.h"

namespace Engine::launcher {

namespace {
    ImVec4 hex(const char* h) {
        if (h[0] == '#') ++h;
        unsigned r = 0, g = 0, b = 0;
        std::sscanf(h, "%02x%02x%02x", &r, &g, &b);
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    }
}

// Same dark minimal palette as the editor — kept identical on purpose so the
// launcher → editor handoff feels continuous.
void apply_theme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding     = ImVec2(16, 16);
    style.FramePadding      = ImVec2(10, 6);
    style.ItemSpacing       = ImVec2(10, 8);
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.PopupRounding     = 4.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;

    const ImVec4 bg            = hex("#0A0A0A");
    const ImVec4 bg_panel      = hex("#141414");
    const ImVec4 accent        = hex("#1C1C1C");
    const ImVec4 hover         = hex("#252525");
    const ImVec4 border        = hex("#1F1F1F");
    const ImVec4 text          = hex("#EDEDED");
    const ImVec4 text_disabled = hex("#7A7A7A");

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]         = bg_panel;
    c[ImGuiCol_PopupBg]          = bg_panel;
    c[ImGuiCol_ChildBg]          = bg_panel;
    c[ImGuiCol_Border]           = border;
    c[ImGuiCol_Text]             = text;
    c[ImGuiCol_TextDisabled]     = text_disabled;
    c[ImGuiCol_FrameBg]          = accent;
    c[ImGuiCol_FrameBgHovered]   = hover;
    c[ImGuiCol_FrameBgActive]    = border;
    c[ImGuiCol_Button]           = border;
    c[ImGuiCol_ButtonHovered]    = hover;
    c[ImGuiCol_ButtonActive]     = accent;
    c[ImGuiCol_Header]           = bg_panel;
    c[ImGuiCol_HeaderHovered]    = hover;
    c[ImGuiCol_HeaderActive]     = accent;
    c[ImGuiCol_Separator]        = border;
    c[ImGuiCol_TitleBg]          = accent;
    c[ImGuiCol_TitleBgActive]    = hover;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    (void)bg;
}

}  // namespace Engine::launcher
