# Probe for C++26 P2996 reflection support when the user asks for it. Done
# once at include time so every target sees a consistent result.
if(ZUES_USE_REFLECTION)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-freflection-latest" ZUES_COMPILER_HAS_REFLECTION)
    if(NOT ZUES_COMPILER_HAS_REFLECTION)
        message(WARNING
            "ZUES_USE_REFLECTION=ON but the compiler does not accept "
            "-freflection-latest. Install the clang-p2996 fork. "
            "Reflection-dependent features disabled for this build.")
        set(ZUES_USE_REFLECTION OFF CACHE BOOL "" FORCE)
    endif()
endif()

# Shared compile flags for every Zues target. Called from zues_add_module
# and from the Core / Editor / MyGame CMakeLists.
function(zues_set_target_flags target)
    target_compile_features(${target} PUBLIC cxx_std_23)

    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN)
        # Pin iterator debug level so STL layout matches across every DLL.
        # MSVC-specific — libc++/libstdc++ ignore this define.
        target_compile_definitions(${target} PUBLIC
            $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=2>
            $<$<NOT:$<CONFIG:Debug>>:_ITERATOR_DEBUG_LEVEL=0>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter
            -Wno-missing-field-initializers
            -fvisibility=hidden
            -fvisibility-inlines-hidden)
    endif()

    if(ZUES_USE_REFLECTION)
        target_compile_definitions(${target} PUBLIC ZUES_HAS_REFLECTION=1)
        target_compile_options(${target} PRIVATE -freflection-latest)
        if(NOT MSVC)
            # P2996 reflection requires C++26 — bump from the project default
            # of C++23 for reflection-enabled targets. libc++ includes some
            # paths that only compile cleanly under -std=c++2c when the meta
            # headers are in scope.
            #
            # CMake 3.28 doesn't know cxx_std_26 as a feature; passing
            # -std=c++2c directly. The flag overrides the c++23 default
            # because compile-options come after compile-features.
            target_compile_options(${target} PUBLIC -std=c++2c)
            # Reflection headers (<experimental/meta>, <meta>) live in libc++.
            # Force libc++ for compile + link, and bake an rpath so the
            # binaries find libc++.so.1 at runtime without external setup.
            target_compile_options(${target} PUBLIC -stdlib=libc++)
            target_link_options   (${target} PUBLIC
                -stdlib=libc++
                -Wl,-rpath,/opt/p2996/clang/lib/x86_64-unknown-linux-gnu)
        endif()
    endif()
endfunction()
