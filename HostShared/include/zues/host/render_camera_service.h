#pragma once

// Host-side IRenderCamera_v1 implementation. Registers the active-camera
// service with the global registry so the renderer module's sprite-render
// system (and anything else) can read the current view transform.
//
// Call register_render_camera_service() once after engine_startup() and
// before any module uses the service.

#include <zues/api.h>

namespace Engine::host {

void register_render_camera_service();

}  // namespace Engine::host
