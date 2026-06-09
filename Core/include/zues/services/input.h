#ifndef ZUES_SERVICE_INPUT_H
#define ZUES_SERVICE_INPUT_H

/*
 * Keyboard + mouse input service. Implemented by zues_window_glfw (or any
 * future windowing module). Old-Unity-style API:
 *
 *   if (input->is_key_down(KEY_W))      ...   // held this frame
 *   if (input->is_key_pressed(KEY_E))   ...   // edge: down-this-frame, up-last
 *   if (input->is_key_released(KEY_E))  ...   // edge: up-this-frame, down-last
 *
 * Key codes mirror GLFW (ASCII for letters/digits + a small set of named
 * constants exported via project_api.h: ZUES_KEY_SPACE, ZUES_KEY_LEFT, ...).
 *
 * `frame_begin` is called by the window module's poll_events implementation
 * BEFORE glfwPollEvents — it snapshots current → previous so the edge
 * helpers compute correctly. Editor code does not need to call it directly.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct IInput_v1 {
    uint32_t abi_version;

    /* Called once per frame BEFORE the platform polls events. Snapshots the
     * current key/button state into "previous" and resets per-frame
     * accumulators (wheel). Window module owns the call. */
    void (*frame_begin)(struct IInput_v1* self);

    /* Keyboard. `key` is GLFW key code. */
    int (*is_key_down)    (struct IInput_v1* self, int key);   /* held this frame */
    int (*is_key_pressed) (struct IInput_v1* self, int key);   /* edge: pressed this frame */
    int (*is_key_released)(struct IInput_v1* self, int key);   /* edge: released this frame */

    /* Cursor position in window pixels (top-left origin, framebuffer space). */
    void (*mouse_pos)(struct IInput_v1* self, float* out_x, float* out_y);

    /* Mouse buttons. button 0 = left, 1 = right, 2 = middle. */
    int (*is_mouse_down)    (struct IInput_v1* self, int button);
    int (*is_mouse_pressed) (struct IInput_v1* self, int button);
    int (*is_mouse_released)(struct IInput_v1* self, int button);

    /* Vertical scroll delta accumulated since last frame_begin. Positive
     * = scroll up. Reset on the next frame_begin. */
    float (*mouse_wheel)(struct IInput_v1* self);
} IInput_v1;

#define ZUES_SERVICE_INPUT          "zues.input"
#define ZUES_SERVICE_INPUT_VERSION  1

#ifdef __cplusplus
}
#endif

#endif /* ZUES_SERVICE_INPUT_H */
