# Real GLFW backend. Creates a window + OpenGL 4.5 context, exposes them via
# IWindow_v1 service for the renderer module to consume.
zues_fetch_glfw()

zues_add_module(zues_window_glfw
    SOURCES src/module.cpp
    LINKS   glfw)
