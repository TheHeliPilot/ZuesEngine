#include <zues/log.h>

#include <cstdio>

namespace Engine {

namespace {
    // Three-letter level tags. Same width without padding hacks, matches the
    // Console panel's renderer for visual consistency between stdout and the
    // in-editor view.
    const char* to_str(LogLevel l) {
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

    EditorLogSinkFn g_editor_sink = nullptr;
}

// Install/clear an additional sink the editor uses to feed its Console
// panel. The stdout/stderr printing still happens unconditionally.
void set_editor_log_sink(EditorLogSinkFn fn) {
    g_editor_sink = fn;
}

void log_write(LogLevel lvl, const char* module, const char* msg) {
    std::FILE* out = (lvl >= LogLevel::Warn) ? stderr : stdout;
    // Format: "LVL  source: message". No brackets, no padding spaces; the
    // double-space between LVL and source is the only spacer we need.
    std::fprintf(out, "%s  %s: %s\n",
                 to_str(lvl),
                 module ? module : "core",
                 msg    ? msg    : "");
    std::fflush(out);

    if (g_editor_sink) g_editor_sink(lvl, module, msg);
}

}  // namespace Engine
