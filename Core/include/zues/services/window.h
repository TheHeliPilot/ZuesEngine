#ifndef ZUES_SERVICE_WINDOW_H
#define ZUES_SERVICE_WINDOW_H

/*
 * Window-module service interface(s).
 *
 * For now: a tiny IWindowProbe_v1 used to verify cross-DLL calls work
 * (module registers it on_load, editor invokes it from on_ready). Real
 * IWindow_v1 (create_window, swap_buffers, poll_events) lands in Phase 3.
 *
 * All service interfaces are pure C structs of function pointers — see
 * docs/04-dll-safety.md for why.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct IWindowProbe_v1 {
    uint32_t abi_version;
    /* Add two ints. Trivial, but proves the module registered, the editor
     * resolved the service, and we can call across the DLL boundary safely. */
    int (*compute)(struct IWindowProbe_v1* self, int x, int y);
} IWindowProbe_v1;

#define ZUES_SERVICE_WINDOW_PROBE         "zues.window.probe"
#define ZUES_SERVICE_WINDOW_PROBE_VERSION 1

/*
 * IWindow_v1 — the real window service. Owned by zues_window_glfw which
 * creates the GLFW window in on_load, registers this service, and tears
 * everything down in on_unload. The window also owns the GL context.
 *
 * The renderer module pulls IWindow_v1 in on_ready to access
 * get_proc_address (for loading OpenGL function pointers) and to drive
 * the swap chain.
 */
typedef struct IWindow_v1 {
    uint32_t abi_version;

    /* Frame */
    int   (*should_close) (struct IWindow_v1* self);
    void  (*request_close)(struct IWindow_v1* self);
    void  (*poll_events)  (struct IWindow_v1* self);
    void  (*swap_buffers) (struct IWindow_v1* self);

    /* Geometry — framebuffer pixels (NOT logical/DPI-scaled). */
    void  (*get_size)(struct IWindow_v1* self, int* out_w, int* out_h);

    /* Native handles + GL context loader. */
    void* (*native_handle)   (struct IWindow_v1* self);
    void* (*get_proc_address)(struct IWindow_v1* self, const char* name);

    /*
     * Post-creation reconfig hooks. The runtime applies project settings
     * (window_width/height, fixed_size, fullscreen, target_aspect, title)
     * AFTER engine_startup -- the window module creates a default 1280x720
     * resizable window in on_load before any project file has been read.
     *
     * All four are no-ops in headless builds (no g_window). All run on the
     * main thread; calling from another thread is undefined.
     */

    /* Resize the existing window. In fullscreen, switches video mode. */
    void  (*set_size)        (struct IWindow_v1* self, int w, int h);

    /* GLFW_RESIZABLE attrib. Disables the user-drag-corner gesture; does
     * not block programmatic set_size. */
    void  (*set_resizable)   (struct IWindow_v1* self, int yes_no);

    /* Toggle borderless/exclusive fullscreen on the primary monitor. When
     * `yes_no == 1`, the window jumps to the closest video mode matching
     * (w,h); pass (0,0) to use the monitor's native resolution. */
    void  (*set_fullscreen)  (struct IWindow_v1* self, int yes_no, int w, int h);

    /* UTF-8 window title. Pass nullptr for an empty title. */
    void  (*set_title)       (struct IWindow_v1* self, const char* utf8);

    /* Lock the user-drag aspect ratio to numerator:denominator (e.g.
     * 16,9). Pass (0,0) to release the lock. Doesn't enforce in fullscreen
     * mode -- callers handle letterboxing via set_viewport_letterbox below. */
    void  (*set_aspect_ratio)(struct IWindow_v1* self, int num, int den);

    /*
     * v3: OS file-drop integration. The window module registers a
     * platform drop handler internally and queues every path the OS
     * delivers. The editor drains the queue each frame to import files
     * the user dragged in from File Explorer / Finder / Nautilus.
     */

    /* Number of paths in the drop queue right now. */
    int   (*dropped_paths_count)(struct IWindow_v1* self);

    /* Copy the i-th dropped path into `out_buf`. Returns the number of
     * bytes written (excluding null terminator). Returns 0 if `i` is
     * out of range or `out_buf` is null. */
    int   (*dropped_path_at)    (struct IWindow_v1* self, int i,
                                  char* out_buf, int buf_size);

    /* Empty the drop queue. Call once after draining. */
    void  (*clear_dropped_paths)(struct IWindow_v1* self);
} IWindow_v1;

#define ZUES_SERVICE_WINDOW_V1         "zues.window.v1"
#define ZUES_SERVICE_WINDOW_V1_VERSION 3

#ifdef __cplusplus
}
#endif

#endif /* ZUES_SERVICE_WINDOW_H */
