#ifndef ZUES_SERVICE_RENDER_CAMERA_H
#define ZUES_SERVICE_RENDER_CAMERA_H

/*
 * Active 2D render camera.
 *
 * Whoever's "driving" the camera (edit mode = editor controls, play mode =
 * the project's Camera2D-following system) writes this every frame via
 * set_active. Whoever's rendering reads it via get_active and computes its
 * world-to-screen transform from the same numbers. Decoupled — render code
 * has no idea who wrote the camera.
 *
 * World units: 1 unit = 1 cm. `pixels_per_unit` converts at zoom 1.0
 * (default 100 px / cm). Final pixel scale = pixels_per_unit * zoom.
 *
 *   screen_x = (world_x - pan_x) * pixels_per_unit * zoom + viewport_w * 0.5
 *   screen_y = (world_y - pan_y) * pixels_per_unit * zoom + viewport_h * 0.5
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Mirrors Engine::components::SortMode. Append-only; never reorder. */
typedef enum ZuesSortMode {
    ZUES_SORT_ORDER_ONLY   = 0,   /* sort by Sprite.layer/order only */
    ZUES_SORT_Y_DESCENDING = 1,   /* top-down 2.5D: lower Y behind, higher Y on top */
    ZUES_SORT_Y_ASCENDING  = 2    /* isometric-style: higher Y behind, lower Y on top */
} ZuesSortMode;

typedef struct ZuesRenderCamera {
    float   pan_x;            /* world-space center the camera looks at (cm) */
    float   pan_y;
    float   zoom;             /* 1.0 = neutral; 2.0 = sprites appear 2x larger */
    float   rotation;         /* radians, around the camera center (unused for now) */
    float   pixels_per_unit;  /* world cm -> screen pixels at zoom 1.0 */
    int32_t viewport_w;       /* destination RT width  in pixels */
    int32_t viewport_h;       /* destination RT height in pixels */
    int32_t sort_mode;        /* ZuesSortMode — how sprites are depth-sorted */
} ZuesRenderCamera;

typedef struct IRenderCamera_v1 {
    uint32_t abi_version;

    /* Replace the active camera. Caller's struct is copied — it doesn't need
     * to outlive the call. */
    void (*set_active)(struct IRenderCamera_v1* self, const ZuesRenderCamera* cam);

    /* Copy the active camera into *out_cam. Safe to call from any thread that
     * isn't racing a set_active (single-threaded-render assumption for v1). */
    void (*get_active)(struct IRenderCamera_v1* self, ZuesRenderCamera* out_cam);
} IRenderCamera_v1;

#define ZUES_SERVICE_RENDER_CAMERA          "zues.render_camera"
#define ZUES_SERVICE_RENDER_CAMERA_VERSION  1

#ifdef __cplusplus
}
#endif

#endif /* ZUES_SERVICE_RENDER_CAMERA_H */
