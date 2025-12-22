// ZuesAPI.h - DLL Export/Import macros for ZuesEngine
// This header defines the ZUES_API macro used to export/import symbols from the Engine DLL

#ifndef ZUES_API_H
#define ZUES_API_H

// Platform detection and DLL export/import macros
#if defined(_WIN32) || defined(_WIN64)
    // Windows
    #ifdef ZUES_ENGINE_EXPORTS
        // Building the Engine DLL - export symbols
        #define ZUES_API __declspec(dllexport)
    #else
        // Using the Engine DLL - import symbols
        #define ZUES_API __declspec(dllimport)
    #endif

    // For C functions that need to be exported
    #ifdef ZUES_ENGINE_EXPORTS
        #define ZUES_API_C extern "C" __declspec(dllexport)
    #else
        #define ZUES_API_C extern "C" __declspec(dllimport)
    #endif
#elif defined(__APPLE__) || defined(__linux__)
    // macOS and Linux - use visibility attributes
    #ifdef ZUES_ENGINE_EXPORTS
        #define ZUES_API __attribute__((visibility("default")))
        #define ZUES_API_C extern "C" __attribute__((visibility("default")))
    #else
        #define ZUES_API
        #define ZUES_API_C extern "C"
    #endif
#else
    // Unknown platform - no special export needed
    #define ZUES_API
    #define ZUES_API_C extern "C"
#endif

// Game DLL export macros (for project/game code)
#if defined(_WIN32) || defined(_WIN64)
    #ifdef GAME_DLL_EXPORTS
        #define GAME_API __declspec(dllexport)
        #define GAME_API_C extern "C" __declspec(dllexport)
    #else
        #define GAME_API __declspec(dllimport)
        #define GAME_API_C extern "C" __declspec(dllimport)
    #endif
#elif defined(__APPLE__) || defined(__linux__)
    #ifdef GAME_DLL_EXPORTS
        #define GAME_API __attribute__((visibility("default")))
        #define GAME_API_C extern "C" __attribute__((visibility("default")))
    #else
        #define GAME_API
        #define GAME_API_C extern "C"
    #endif
#else
    #define GAME_API
    #define GAME_API_C extern "C"
#endif

// Macro to suppress unused parameter warnings
#define ZUES_UNUSED(x) (void)(x)

// Version information
#define ZUES_ENGINE_VERSION_MAJOR 1
#define ZUES_ENGINE_VERSION_MINOR 0
#define ZUES_ENGINE_VERSION_PATCH 0

#endif // ZUES_API_H
