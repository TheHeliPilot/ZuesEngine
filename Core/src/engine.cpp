#include <zues/engine.h>
#include <zues/events.h>
#include <zues/log.h>
#include <zues/service.h>

#include "module_loader_internal.h"

namespace Engine {

namespace {
    ServiceRegistry*          g_services = nullptr;
    EventBus*                 g_events   = nullptr;
    internal::ModuleRegistry* g_modules  = nullptr;
    bool                      g_running  = false;
}

Result engine_startup(const EngineStartupDesc& desc) {
    if (g_running) return Result::AlreadyExists;

    g_services = new ServiceRegistry();
    g_events   = new EventBus();
    g_modules  = new internal::ModuleRegistry();
    g_running  = true;

    log_write(LogLevel::Info, "core", "Zues engine starting");

    ModuleContext ctx{g_services, g_events};
    g_modules->load_all(desc.modules_dir, &ctx);
    g_modules->signal_ready(&ctx);

    log_write(LogLevel::Info, "core", "Zues engine ready");
    return Result::Ok;
}

void engine_shutdown() {
    if (!g_running) return;
    log_write(LogLevel::Info, "core", "Zues engine shutting down");

    ModuleContext ctx{g_services, g_events};
    if (g_modules) {
        g_modules->unload_all(&ctx);
        delete g_modules;
        g_modules = nullptr;
    }

    delete g_events;   g_events   = nullptr;
    delete g_services; g_services = nullptr;
    g_running = false;
}

ServiceRegistry* services() { return g_services; }
EventBus*        events()   { return g_events; }

}  // namespace Engine
