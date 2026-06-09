#pragma once

// ZUES_API marks every symbol in zues_core.dll that is visible to consumers
// (editor, modules). Hidden visibility is the default via CMake; ZUES_API
// opts symbols into export.
//
// See docs/04-dll-safety.md.

#if defined(_WIN32)
    #define ZUES_EXPORT __declspec(dllexport)
    #define ZUES_IMPORT __declspec(dllimport)
#else
    #define ZUES_EXPORT __attribute__((visibility("default")))
    #define ZUES_IMPORT
#endif

#if defined(ZUES_BUILDING_CORE)
    #define ZUES_API ZUES_EXPORT
#else
    #define ZUES_API ZUES_IMPORT
#endif

// Every module's zues_module_entry() uses this.
#define ZUES_MODULE_EXPORT extern "C" ZUES_EXPORT
