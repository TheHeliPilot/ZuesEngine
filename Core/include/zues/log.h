#pragma once
#include <zues/api.h>

namespace Engine {

enum class LogLevel : unsigned {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5,
};

// ZUES_STRIP_LOGS turns log_write into an inline no-op for THIS translation
// unit. The release runtime + host_shared get this so log calls compile to
// nothing -- no string literals retained, no DLL hop. The actual exported
// log_write symbol still lives in zues_core.dll for any TU that didn't
// strip (engine modules, editor).
#if defined(ZUES_STRIP_LOGS)
inline void log_write(LogLevel, const char*, const char*) {}
#else
ZUES_API void log_write(LogLevel lvl, const char* module, const char* msg);
#endif

// Editor-side hook: install a sink that the core's log_write fans out to
// in addition to stdout/stderr. Pass nullptr to remove. Single sink slot
// for now (the editor is the only consumer).
using EditorLogSinkFn = void (*)(LogLevel level, const char* source, const char* message);
ZUES_API void set_editor_log_sink(EditorLogSinkFn fn);

}  // namespace Engine

// Modules and core call-sites use these. ZUES_MODULE_NAME is defined as a
// string literal in module builds; core falls through to "core".
#if defined(ZUES_MODULE_NAME)
    #define ZUES_LOG_SOURCE ZUES_MODULE_NAME
#else
    #define ZUES_LOG_SOURCE "core"
#endif

#define ZUES_LOG_TRACE(msg) ::Engine::log_write(::Engine::LogLevel::Trace, ZUES_LOG_SOURCE, msg)
#define ZUES_LOG_DEBUG(msg) ::Engine::log_write(::Engine::LogLevel::Debug, ZUES_LOG_SOURCE, msg)
#define ZUES_LOG_INFO(msg)  ::Engine::log_write(::Engine::LogLevel::Info,  ZUES_LOG_SOURCE, msg)
#define ZUES_LOG_WARN(msg)  ::Engine::log_write(::Engine::LogLevel::Warn,  ZUES_LOG_SOURCE, msg)
#define ZUES_LOG_ERROR(msg) ::Engine::log_write(::Engine::LogLevel::Error, ZUES_LOG_SOURCE, msg)
#define ZUES_LOG_FATAL(msg) ::Engine::log_write(::Engine::LogLevel::Fatal, ZUES_LOG_SOURCE, msg)
