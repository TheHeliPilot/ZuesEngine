#include <zues/host/render_camera_service.h>

#include <zues/engine.h>
#include <zues/log.h>
#include <zues/service.h>
#include <zues/services/render_camera.h>

#include <cstring>

namespace Engine::host {

namespace {
    // Single-process active camera. Editor's camera-publish system writes it
    // each frame; the renderer module's sprite-render system reads it.
    ZuesRenderCamera g_camera = {
        /*.pan_x=*/0.0f, /*.pan_y=*/0.0f,
        /*.zoom=*/1.0f, /*.rotation=*/0.0f,
        /*.pixels_per_unit=*/100.0f,
        /*.viewport_w=*/0, /*.viewport_h=*/0,
        /*.sort_mode=*/0
    };

    void set_active(IRenderCamera_v1*, const ZuesRenderCamera* cam) {
        if (cam) g_camera = *cam;
    }

    void get_active(IRenderCamera_v1*, ZuesRenderCamera* out_cam) {
        if (out_cam) *out_cam = g_camera;
    }

    IRenderCamera_v1 g_iface = {
        /*.abi_version=*/ ZUES_SERVICE_RENDER_CAMERA_VERSION,
        /*.set_active =*/ set_active,
        /*.get_active =*/ get_active,
    };
}

void register_render_camera_service() {
    auto* sr = Engine::services();
    if (!sr) {
        ZUES_LOG_ERROR("render_camera: service registry unavailable");
        return;
    }
    sr->register_service(ZUES_SERVICE_RENDER_CAMERA,
                         ZUES_SERVICE_RENDER_CAMERA_VERSION,
                         &g_iface);
}

}  // namespace Engine::host
