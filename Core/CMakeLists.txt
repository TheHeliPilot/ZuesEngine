set(ZUES_CORE_PUBLIC_HEADERS
    include/zues/api.h
    include/zues/types.h
    include/zues/guid.h
    include/zues/asset.h
    include/zues/engine.h
    include/zues/module.h
    include/zues/service.h
    include/zues/events.h
    include/zues/log.h
    include/zues/math.h
    include/zues/math/vec2.h
    include/zues/math/vec3.h
    include/zues/math/vec4.h
    include/zues/math/mat4.h
    include/zues/math/quat.h
    include/zues/math/rect.h
    include/zues/math/color.h
    include/zues/ecs.h
    include/zues/ecs/entity.h
    include/zues/ecs/reflection.h
    include/zues/ecs/component_type.h
    include/zues/ecs/world.h
    include/zues/components.h
    include/zues/components/transform.h
    include/zues/components/hierarchy.h
    include/zues/components/name.h
    include/zues/components/lifetime.h
    include/zues/components/render.h
    include/zues/services/window.h
    include/zues/services/renderer_2d.h
    include/zues/services/debug_draw.h
    include/zues/project.h)

set(ZUES_CORE_SOURCES
    src/engine.cpp
    src/service.cpp
    src/events.cpp
    src/log.cpp
    src/guid.cpp
    src/animation.cpp
    src/asset_registry.cpp
    src/audio_cue.cpp
    src/module_loader.cpp
    src/math_impl.cpp
    src/ecs/archetype.cpp
    src/ecs/world.cpp
    src/project.cpp)

add_library(zues_core SHARED ${ZUES_CORE_SOURCES} ${ZUES_CORE_PUBLIC_HEADERS})
add_library(zues::core ALIAS zues_core)

target_include_directories(zues_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# glm is a private dependency of Core — used internally by zues::math impls.
# Public Core headers never include glm; users only see Engine::math types.
zues_fetch_glm()
target_link_libraries(zues_core PRIVATE glm::glm)

# nlohmann/json is private too — used for the .zuesproject loader.
zues_fetch_json()
target_link_libraries(zues_core PRIVATE nlohmann_json::nlohmann_json)

target_compile_definitions(zues_core PRIVATE ZUES_BUILDING_CORE=1)

set_target_properties(zues_core PROPERTIES
    CXX_VISIBILITY_PRESET      hidden
    VISIBILITY_INLINES_HIDDEN  ON
    PREFIX                     ""
    OUTPUT_NAME                "zues_core")

zues_set_target_flags(zues_core)
