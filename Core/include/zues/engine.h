#pragma once
#include <zues/api.h>
#include <zues/types.h>

namespace Engine {

class ServiceRegistry;
class EventBus;

struct EngineStartupDesc {
    const char* modules_dir = "";    // folder to scan for module DLLs
    const char* project_dll = "";    // optional: user project.dll to load at startup
};

// Starts Core subsystems and loads modules from desc.modules_dir.
// Subsequent calls before engine_shutdown return AlreadyExists.
ZUES_API Result engine_startup(const EngineStartupDesc& desc);

// Tears down modules in reverse load order, then Core subsystems.
ZUES_API void engine_shutdown();

// Access the global registry and bus. Null before startup, null after shutdown.
ZUES_API ServiceRegistry* services();
ZUES_API EventBus*        events();

}  // namespace Engine
