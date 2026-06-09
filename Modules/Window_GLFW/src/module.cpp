#include <zues/api.h>
#include <zues/log.h>
#include <zues/module.h>
#include <zues/service.h>
#include <zues/services/input.h>
#include <zues/services/window.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Engine;

// =============================================================================
// Module-local state. One window per process.
// =============================================================================

namespace {
    GLFWwindow* g_window     = nullptr;
    bool        g_glfw_inited = false;

    // ---- Input state ----------------------------------------------------
    // Two parallel arrays per channel: `now` updated by GLFW callbacks as
    // events arrive, `prev` snapshotted at frame start. Edge helpers use
    // the diff. Wheel is an accumulator (resets each frame).
    constexpr int MAX_KEY = GLFW_KEY_LAST + 1;
    constexpr int MAX_MB  = GLFW_MOUSE_BUTTON_LAST + 1;

    struct InputState {
        uint8_t keys_now [MAX_KEY] = {};
        uint8_t keys_prev[MAX_KEY] = {};
        uint8_t mb_now   [MAX_MB]  = {};
        uint8_t mb_prev  [MAX_MB]  = {};
        float   mouse_x = 0.0f, mouse_y = 0.0f;
        float   wheel_accum = 0.0f;
    };
    InputState g_input;

    void glfw_error_callback(int code, const char* desc) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "GLFW error %d: %s", code, desc ? desc : "(null)");
        log_write(LogLevel::Error, "zues_window_glfw", buf);
    }

    void glfw_key_callback(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
        if (key >= 0 && key < MAX_KEY) {
            g_input.keys_now[key] = (action != GLFW_RELEASE) ? 1 : 0;
        }
        // (No ESC-to-close: too aggressive when ESC is also used for
        //  cancelling popups, exiting fullscreen modes, deselecting, etc.
        //  Quit is via the close button or Alt+F4 / IWindow_v1::request_close.)
        (void)w;
    }
    void glfw_cursor_callback(GLFWwindow*, double x, double y) {
        g_input.mouse_x = static_cast<float>(x);
        g_input.mouse_y = static_cast<float>(y);
    }
    void glfw_mouse_button_callback(GLFWwindow*, int button, int action, int /*mods*/) {
        if (button >= 0 && button < MAX_MB) {
            g_input.mb_now[button] = (action != GLFW_RELEASE) ? 1 : 0;
        }
    }
    void glfw_scroll_callback(GLFWwindow*, double /*xoff*/, double yoff) {
        g_input.wheel_accum += static_cast<float>(yoff);
    }

    // ---- OS file drop ---------------------------------------------------
    // GLFW delivers (count, path[]) to one global callback; we fan it out
    // into a queue the editor drains each frame. Paths are utf-8 absolute.
    std::vector<std::string> g_dropped_paths;

    void glfw_drop_callback(GLFWwindow*, int count, const char** paths) {
        if (!paths) return;
        for (int i = 0; i < count; ++i) {
            if (paths[i]) g_dropped_paths.emplace_back(paths[i]);
        }
    }
}

// =============================================================================
// IInput_v1 implementation.
// =============================================================================

static void input_frame_begin(IInput_v1*) {
    std::memcpy(g_input.keys_prev, g_input.keys_now, sizeof(g_input.keys_now));
    std::memcpy(g_input.mb_prev,   g_input.mb_now,   sizeof(g_input.mb_now));
    g_input.wheel_accum = 0.0f;
}

static int input_is_key_down(IInput_v1*, int key) {
    return (key >= 0 && key < MAX_KEY) ? g_input.keys_now[key] : 0;
}
static int input_is_key_pressed(IInput_v1*, int key) {
    if (key < 0 || key >= MAX_KEY) return 0;
    return g_input.keys_now[key] && !g_input.keys_prev[key];
}
static int input_is_key_released(IInput_v1*, int key) {
    if (key < 0 || key >= MAX_KEY) return 0;
    return !g_input.keys_now[key] && g_input.keys_prev[key];
}
static void input_mouse_pos(IInput_v1*, float* ox, float* oy) {
    if (ox) *ox = g_input.mouse_x;
    if (oy) *oy = g_input.mouse_y;
}
static int input_is_mouse_down(IInput_v1*, int b) {
    return (b >= 0 && b < MAX_MB) ? g_input.mb_now[b] : 0;
}
static int input_is_mouse_pressed(IInput_v1*, int b) {
    if (b < 0 || b >= MAX_MB) return 0;
    return g_input.mb_now[b] && !g_input.mb_prev[b];
}
static int input_is_mouse_released(IInput_v1*, int b) {
    if (b < 0 || b >= MAX_MB) return 0;
    return !g_input.mb_now[b] && g_input.mb_prev[b];
}
static float input_mouse_wheel(IInput_v1*) { return g_input.wheel_accum; }

static IInput_v1 g_iinput = {
    /* .abi_version       */ ZUES_SERVICE_INPUT_VERSION,
    /* .frame_begin       */ input_frame_begin,
    /* .is_key_down       */ input_is_key_down,
    /* .is_key_pressed    */ input_is_key_pressed,
    /* .is_key_released   */ input_is_key_released,
    /* .mouse_pos         */ input_mouse_pos,
    /* .is_mouse_down     */ input_is_mouse_down,
    /* .is_mouse_pressed  */ input_is_mouse_pressed,
    /* .is_mouse_released */ input_is_mouse_released,
    /* .mouse_wheel       */ input_mouse_wheel,
};

// =============================================================================
// IWindow_v1 implementation — pure C function pointers.
// =============================================================================

static int  fn_should_close(IWindow_v1*) {
    return g_window ? glfwWindowShouldClose(g_window) : 1;
}
static void fn_request_close(IWindow_v1*) {
    if (g_window) glfwSetWindowShouldClose(g_window, GLFW_TRUE);
}
static void fn_poll_events(IWindow_v1*) {
    // Snapshot input BEFORE pumping events so edge helpers see the right
    // diff. GLFW callbacks then update keys_now / mb_now / wheel_accum.
    input_frame_begin(nullptr);
    glfwPollEvents();
}
static void fn_swap_buffers(IWindow_v1*) { if (g_window) glfwSwapBuffers(g_window); }

static void fn_get_size(IWindow_v1*, int* out_w, int* out_h) {
    if (!g_window) { if (out_w) *out_w = 0; if (out_h) *out_h = 0; return; }
    int w = 0, h = 0;
    glfwGetFramebufferSize(g_window, &w, &h);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void* fn_native_handle(IWindow_v1*) { return g_window; }

static void* fn_get_proc_address(IWindow_v1*, const char* name) {
    if (!name) return nullptr;
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

// ---- Post-creation reconfig (v2 additions) ----------------------------------
// All silent no-ops if the window failed to create (g_window == nullptr) so
// callers don't have to null-check before applying project settings.

namespace {
    // Cache the windowed-mode geometry so toggling fullscreen and back
    // restores the user's previous size + position rather than always
    // collapsing to defaults.
    int g_windowed_x = 0, g_windowed_y = 0;
    int g_windowed_w = 1280, g_windowed_h = 720;
}

static void fn_set_size(IWindow_v1*, int w, int h) {
    if (!g_window || w <= 0 || h <= 0) return;
    glfwSetWindowSize(g_window, w, h);
}

static void fn_set_resizable(IWindow_v1*, int yes_no) {
    if (!g_window) return;
    glfwSetWindowAttrib(g_window, GLFW_RESIZABLE, yes_no ? GLFW_TRUE : GLFW_FALSE);
}

static void fn_set_fullscreen(IWindow_v1*, int yes_no, int w, int h) {
    if (!g_window) return;
    GLFWmonitor* cur = glfwGetWindowMonitor(g_window);
    const bool is_fs = (cur != nullptr);
    if (yes_no && !is_fs) {
        // Snapshot windowed geometry so the toggle-back path restores it.
        glfwGetWindowPos (g_window, &g_windowed_x, &g_windowed_y);
        glfwGetWindowSize(g_window, &g_windowed_w, &g_windowed_h);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (!mon) return;
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        if (!mode) return;
        const int target_w = (w > 0) ? w : mode->width;
        const int target_h = (h > 0) ? h : mode->height;
        glfwSetWindowMonitor(g_window, mon, 0, 0,
                              target_w, target_h, mode->refreshRate);
    } else if (!yes_no && is_fs) {
        glfwSetWindowMonitor(g_window, nullptr,
                              g_windowed_x, g_windowed_y,
                              g_windowed_w, g_windowed_h, 0);
    } else if (yes_no && is_fs && w > 0 && h > 0) {
        // Already fullscreen; just change the video mode.
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
        if (mon && mode)
            glfwSetWindowMonitor(g_window, mon, 0, 0, w, h, mode->refreshRate);
    }
}

static void fn_set_title(IWindow_v1*, const char* utf8) {
    if (!g_window) return;
    glfwSetWindowTitle(g_window, utf8 ? utf8 : "");
}

static void fn_set_aspect_ratio(IWindow_v1*, int num, int den) {
    if (!g_window) return;
    if (num <= 0 || den <= 0) {
        glfwSetWindowAspectRatio(g_window, GLFW_DONT_CARE, GLFW_DONT_CARE);
    } else {
        glfwSetWindowAspectRatio(g_window, num, den);
    }
}

// ---- v3: OS file drop --------------------------------------------------

static int fn_dropped_paths_count(IWindow_v1*) {
    return static_cast<int>(g_dropped_paths.size());
}

static int fn_dropped_path_at(IWindow_v1*, int i, char* out, int n) {
    if (!out || n <= 0) return 0;
    if (i < 0 || i >= (int)g_dropped_paths.size()) { out[0] = 0; return 0; }
    const std::string& p = g_dropped_paths[i];
    int copy = std::min<int>(n - 1, static_cast<int>(p.size()));
    std::memcpy(out, p.data(), static_cast<size_t>(copy));
    out[copy] = 0;
    return copy;
}

static void fn_clear_dropped_paths(IWindow_v1*) {
    g_dropped_paths.clear();
}

static IWindow_v1 g_iwindow = {
    /* .abi_version      */ ZUES_SERVICE_WINDOW_V1_VERSION,
    /* .should_close     */ fn_should_close,
    /* .request_close    */ fn_request_close,
    /* .poll_events      */ fn_poll_events,
    /* .swap_buffers     */ fn_swap_buffers,
    /* .get_size         */ fn_get_size,
    /* .native_handle    */ fn_native_handle,
    /* .get_proc_address */ fn_get_proc_address,
    /* .set_size            */ fn_set_size,
    /* .set_resizable       */ fn_set_resizable,
    /* .set_fullscreen      */ fn_set_fullscreen,
    /* .set_title           */ fn_set_title,
    /* .set_aspect_ratio    */ fn_set_aspect_ratio,
    /* .dropped_paths_count */ fn_dropped_paths_count,
    /* .dropped_path_at     */ fn_dropped_path_at,
    /* .clear_dropped_paths */ fn_clear_dropped_paths,
};

// =============================================================================
// IWindowProbe_v1 (kept for the cross-DLL service-call sanity test).
// =============================================================================

static int probe_compute(IWindowProbe_v1*, int x, int y) { return x + y; }

static IWindowProbe_v1 g_iprobe = {
    /* .abi_version */ ZUES_SERVICE_WINDOW_PROBE_VERSION,
    /* .compute     */ probe_compute,
};

// =============================================================================
// Module lifecycle.
// =============================================================================

static void on_load(ModuleContext* ctx) {
    ZUES_LOG_INFO("window_glfw on_load");
    if (!ctx || !ctx->services) return;

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        ZUES_LOG_WARN("glfwInit failed — running without a window. "
                      "(headless / no display? IWindow_v1 won't be registered.)");
        // Still register the probe service so the cross-DLL smoke test runs.
        ctx->services->register_service(ZUES_SERVICE_WINDOW_PROBE,
                                        ZUES_SERVICE_WINDOW_PROBE_VERSION,
                                        &g_iprobe);
        return;
    }
    g_glfw_inited = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE,   GLFW_TRUE);

    g_window = glfwCreateWindow(1280, 720, "Zues Engine", nullptr, nullptr);
    if (!g_window) {
        ZUES_LOG_ERROR("glfwCreateWindow failed");
        glfwTerminate();
        g_glfw_inited = false;
        ctx->services->register_service(ZUES_SERVICE_WINDOW_PROBE,
                                        ZUES_SERVICE_WINDOW_PROBE_VERSION,
                                        &g_iprobe);
        return;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);    // vsync
    glfwSetKeyCallback        (g_window, glfw_key_callback);
    glfwSetCursorPosCallback  (g_window, glfw_cursor_callback);
    glfwSetMouseButtonCallback(g_window, glfw_mouse_button_callback);
    glfwSetScrollCallback     (g_window, glfw_scroll_callback);
    glfwSetDropCallback       (g_window, glfw_drop_callback);

    // Register services. ImGui_ImplGlfw chains to these callbacks during
    // its own init (called later by the editor) — order matters: install
    // ours first so ImGui sees them as "previous" and chains correctly.
    ctx->services->register_service(ZUES_SERVICE_WINDOW_V1,
                                    ZUES_SERVICE_WINDOW_V1_VERSION,
                                    &g_iwindow);
    ctx->services->register_service(ZUES_SERVICE_WINDOW_PROBE,
                                    ZUES_SERVICE_WINDOW_PROBE_VERSION,
                                    &g_iprobe);
    ctx->services->register_service(ZUES_SERVICE_INPUT,
                                    ZUES_SERVICE_INPUT_VERSION,
                                    &g_iinput);
    ZUES_LOG_INFO("window_glfw: GLFW window created at 1280x720");
}

static void on_ready (ModuleContext*)        { ZUES_LOG_INFO("window_glfw on_ready"); }

static void on_unload(ModuleContext*) {
    ZUES_LOG_INFO("window_glfw on_unload");
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    if (g_glfw_inited) {
        glfwTerminate();
        g_glfw_inited = false;
    }
}

static const ModuleInfo INFO = {
    .name        = "zues_window_glfw",
    .version     = "0.1.0",
    .abi_version = ZUES_MODULE_ABI_VERSION,
    .on_load     = on_load,
    .on_ready    = on_ready,
    .on_update   = nullptr,
    .on_unload   = on_unload,
};

ZUES_MODULE_EXPORT const ModuleInfo* zues_module_entry() { return &INFO; }
