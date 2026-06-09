#include "editor.h"

#include <imgui.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Engine::editor {

namespace {

// ---------------------------------------------------------------------------
// Level helpers
// ---------------------------------------------------------------------------

ImVec4 color_for_level(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        case LogLevel::Debug: return ImVec4(0.62f, 0.78f, 0.95f, 1.0f);
        case LogLevel::Info:  return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
        case LogLevel::Warn:  return ImVec4(0.98f, 0.78f, 0.30f, 1.0f);
        case LogLevel::Error: return ImVec4(0.96f, 0.40f, 0.36f, 1.0f);
        case LogLevel::Fatal: return ImVec4(1.00f, 0.25f, 0.25f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

const char* short_label_for_level(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRC";
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        case LogLevel::Error: return "ERR";
        case LogLevel::Fatal: return "FTL";
    }
    return "???";
}

// ---------------------------------------------------------------------------
// Timestamp formatter
// ---------------------------------------------------------------------------

void format_timestamp(double t_s, char out[16]) {
    const long long total = static_cast<long long>(t_s);
    const int h   = static_cast<int>((total / 3600) % 24);
    const int m   = static_cast<int>((total / 60)   % 60);
    const int sec = static_cast<int>( total          % 60);
    std::snprintf(out, 16, "%02d:%02d:%02d", h, m, sec);
}

// Format a line into a flat string for clipboard / Copy All.
std::string format_line_for_copy(const LogLine& line) {
    char ts[16]; format_timestamp(line.time_s, ts);
    char head[256];
    std::snprintf(head, sizeof(head), "%s  %s  %s: ",
                  ts, short_label_for_level(line.level), line.source.c_str());
    return std::string(head) + line.message;
}

// ---------------------------------------------------------------------------
// Per-source color via FNV-1a hash -> HSL with fixed S/L for dark-theme readability
// ---------------------------------------------------------------------------

// FNV-1a 32-bit hash of an ASCII string.
static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619u;
    }
    return h;
}

// HSL -> RGB conversion (H in [0,1), S and L in [0,1]).
static float hue2rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f)       return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

static ImVec4 color_for_source(const char* src) {
    const uint32_t h = fnv1a(src);
    // Spread hue across the full wheel; fixed S=0.65, L=0.68 gives bright but
    // not saturated colors that stay readable on the near-black console bg.
    const float hue = static_cast<float>(h & 0xFFFF) / 65535.0f;
    const float sat = 0.65f;
    const float lit = 0.68f;

    const float q = lit < 0.5f ? lit * (1.0f + sat) : lit + sat - lit * sat;
    const float p = 2.0f * lit - q;
    return ImVec4(
        hue2rgb(p, q, hue + 1.0f/3.0f),
        hue2rgb(p, q, hue),
        hue2rgb(p, q, hue - 1.0f/3.0f),
        1.0f
    );
}

// ---------------------------------------------------------------------------
// Dot-depth (capped)
// ---------------------------------------------------------------------------

static int dot_depth(const std::string& src, int cap = 5) {
    int d = 0;
    for (char c : src) {
        if (c == '.') { if (++d >= cap) return cap; }
    }
    return d;
}

// ---------------------------------------------------------------------------
// Case-insensitive substring search (ASCII only, as per constraints).
// ---------------------------------------------------------------------------

static bool icontains(const char* haystack, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    if (!haystack) return false;
    // Walk haystack; for each position try a case-insensitive match.
    for (const char* h = haystack; *h; ++h) {
        const char* hh = h;
        const char* nn = needle;
        while (*hh && *nn && std::tolower((unsigned char)*hh) == std::tolower((unsigned char)*nn)) {
            ++hh; ++nn;
        }
        if (*nn == '\0') return true;
    }
    return false;
}

} // namespace (anonymous)

// ===========================================================================
// draw_console_panel
// ===========================================================================

void draw_console_panel(EditorState& s) {
    if (!s.show_console) return;
    if (!ImGui::Begin("Console", &s.show_console)) { ImGui::End(); return; }

    // ---- Toolbar -----------------------------------------------------------
    if (ImGui::Button("Clear")) s.log.clear();
    ImGui::SameLine();
    if (ImGui::Button("Copy All")) {
        std::string all;
        all.reserve(s.log.entries().size() * 64);
        for (const auto& line : s.log.entries()) {
            if (!s.log.show_level[(int)line.level]) continue;
            all += format_line_for_copy(line);
            all += '\n';
        }
        if (!all.empty()) ImGui::SetClipboardText(all.c_str());
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy all visible lines to clipboard");

    // Filter input — ~150 px wide, placed to the right of Copy All.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputTextWithHint("##filter", "filter...", s.log_filter, sizeof(s.log_filter));

    // Level checkboxes on the same line after the filter.
    ImGui::SameLine();
    ImGui::Checkbox("Trace", &s.log.show_level[(int)LogLevel::Trace]); ImGui::SameLine();
    ImGui::Checkbox("Debug", &s.log.show_level[(int)LogLevel::Debug]); ImGui::SameLine();
    ImGui::Checkbox("Info",  &s.log.show_level[(int)LogLevel::Info ]); ImGui::SameLine();
    ImGui::Checkbox("Warn",  &s.log.show_level[(int)LogLevel::Warn ]); ImGui::SameLine();
    ImGui::Checkbox("Error", &s.log.show_level[(int)LogLevel::Error]); ImGui::SameLine();
    ImGui::Checkbox("Fatal", &s.log.show_level[(int)LogLevel::Fatal]);

    ImGui::Separator();

    // ---- Column layout -----------------------------------------------------
    // COL_LEVEL   = fixed x-offset for the LVL badge.
    // COL_SOURCE  = base x-offset for the source name (before depth indent).
    // COL_MSG     = fixed x-offset for the message column.
    //
    // Depth-based indent shifts SOURCE only; COL_MSG is placed well past the
    // deepest likely source (5 levels * 12 px = 60 px extra) so messages stay
    // column-aligned regardless of how nested the source is.
    //
    // Pixel units: ImGui SameLine(x) treats x as offset from the left edge of
    // the current window (ImGui cursor X). The child window starts at x=0 in
    // cursor space, so these map directly.
    constexpr float COL_LEVEL   =  70.0f;
    constexpr float COL_SOURCE  = 110.0f;
    constexpr float DEPTH_STEP  =  12.0f;
    constexpr float COL_MSG     = 310.0f;  // 110 + 5*12 indent budget + ~140 for label

    const ImVec4 dim_col {0.55f, 0.58f, 0.62f, 1.0f};   // timestamp dim grey

    // Row banding: faint white tint every other row so long lists are skimmable.
    const ImVec4 band_tint {1.0f, 1.0f, 1.0f, 0.025f};

    // Message wrap position: right edge of the window minus a small margin.
    // Computed once per frame (window size may change).
    const float wrap_x = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 8.0f;

    if (ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0),
                          ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const auto& entries = s.log.entries();
        const bool  has_filter = s.log_filter[0] != '\0';
        size_t visible_row = 0;   // counts only rows that pass all filters

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& line = entries[i];

            // Level filter
            if (!s.log.show_level[(int)line.level]) continue;

            // Text filter (case-insensitive, source OR message must match)
            if (has_filter) {
                const bool src_ok = icontains(line.source.c_str(),  s.log_filter);
                const bool msg_ok = icontains(line.message.c_str(), s.log_filter);
                if (!src_ok && !msg_ok) continue;
            }

            ImGui::PushID(static_cast<int>(i));

            // Row anchor — we need this before drawing the Selectable so we
            // can paint the band rect behind it.
            const ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float  row_h   = ImGui::GetTextLineHeight();
            const float  row_w   = ImGui::GetContentRegionAvail().x;

            // Subtle alternating row band (every other *visible* row).
            if (visible_row & 1u) {
                draw_list->AddRectFilled(
                    row_pos,
                    ImVec2(row_pos.x + row_w, row_pos.y + row_h),
                    ImGui::ColorConvertFloat4ToU32(band_tint));
            }
            ++visible_row;

            // Invisible Selectable spanning the full row for hover + right-click.
            ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0, row_h));

            if (ImGui::BeginPopupContextItem("##row_ctx")) {
                if (ImGui::MenuItem("Copy line")) {
                    ImGui::SetClipboardText(format_line_for_copy(line).c_str());
                }
                if (ImGui::MenuItem("Copy message only")) {
                    ImGui::SetClipboardText(line.message.c_str());
                }
                ImGui::EndPopup();
            }

            // Reset cursor to the start of this row so colored segments overlay
            // the (transparent) Selectable.
            ImGui::SetCursorScreenPos(row_pos);

            // Timestamp (dim)
            char ts[16]; format_timestamp(line.time_s, ts);
            ImGui::TextColored(dim_col, "%s", ts);

            // Level badge
            ImGui::SameLine(COL_LEVEL);
            ImGui::TextColored(color_for_level(line.level), "%s",
                               short_label_for_level(line.level));

            // Source name — deterministic per-source color, indented by dot-depth.
            const int   depth   = dot_depth(line.source);
            const float src_x   = COL_SOURCE + static_cast<float>(depth) * DEPTH_STEP;
            ImGui::SameLine(src_x);
            ImGui::TextColored(color_for_source(line.source.c_str()),
                               "%s", line.source.c_str());

            // Message — wrapped at the right edge so long lines fold rather than
            // pushing horizontal scroll.
            ImGui::SameLine(COL_MSG);
            ImGui::PushTextWrapPos(wrap_x);
            ImGui::TextColored(color_for_level(line.level), "%s", line.message.c_str());
            ImGui::PopTextWrapPos();

            ImGui::PopID();
        }

        // Auto-scroll to bottom when at-or-near the bottom already.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace Engine::editor
