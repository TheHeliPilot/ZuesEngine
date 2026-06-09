#include "editor.h"

#include <zues/log.h>

#include <chrono>
#include <mutex>

namespace Engine::editor {

namespace {
    // Single-process state. The log sink is global by design — every
    // log_write goes through it. A mutex keeps appends safe against future
    // worker threads.
    std::mutex      g_mutex;
    LogRingBuffer*  g_dest = nullptr;

    void log_callback(LogLevel lvl, const char* source, const char* msg) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_dest) g_dest->push(lvl, source, msg);
    }
}

void LogRingBuffer::push(LogLevel lvl, const char* source, const char* msg) {
    if (m_entries.size() >= MAX_ENTRIES) {
        // Drop oldest. vector erase from front is O(n) — acceptable since
        // we only do it once per overflow and the buffer caps at 4k.
        m_entries.erase(m_entries.begin());
    }
    // Wall-clock seconds since process start. Cheap, monotonic, doesn't need
    // a system clock conversion until render time (we just want HH:MM:SS).
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    const double t = std::chrono::duration<double>(clock::now() - t0).count();
    m_entries.push_back({lvl,
                         source ? std::string(source) : std::string{},
                         msg    ? std::string(msg)    : std::string{},
                         t});
}

}  // namespace Engine::editor

// =============================================================================
// We register the editor sink by overriding zues::log_write directly. Core's
// log.cpp implementation prints to stdout/stderr; we want BOTH that and the
// in-editor capture. Cleanest path: provide a hookable sink via the log
// system itself. Until that exists, the editor wraps log_write at link time
// — but that requires Core to be aware of the sink.
//
// Pragmatic v1: we expose install/uninstall here, and Core's log.cpp gains
// a tiny extension to call out to a sink if one is set. See
// Core/src/log.cpp for the matching hook.
// =============================================================================

namespace Engine::editor {

void install_log_sink(LogRingBuffer* dest) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_dest = dest;
    }
    Engine::set_editor_log_sink(log_callback);
}

void uninstall_log_sink() {
    Engine::set_editor_log_sink(nullptr);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dest = nullptr;
}

}  // namespace Engine::editor
