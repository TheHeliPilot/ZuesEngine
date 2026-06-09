find_package(OpenGL REQUIRED)

# stb_image (header-only) for texture loading.
zues_fetch_stb()

zues_add_module(zues_renderer_gl
    SOURCES
        src/module.cpp
        src/gl_loader.cpp
        src/sprite_batcher.cpp
        src/texture_registry.cpp
        src/render_target.cpp
        src/font_registry.cpp
    LINKS OpenGL::GL)

target_include_directories(zues_renderer_gl PRIVATE ${stb_SOURCE_DIR})
