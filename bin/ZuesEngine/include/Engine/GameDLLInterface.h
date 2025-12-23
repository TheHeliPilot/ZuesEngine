// GameDLLInterface.h - Interface that game/project DLLs must implement for hot-reload
// This file should be included by both the Engine and the Game project

#ifndef ZUES_GAME_DLL_INTERFACE_H
#define ZUES_GAME_DLL_INTERFACE_H

#include "ZuesAPI.h"
#include "ECS/World.h"

// Version of the interface - increment when interface changes
#define GAME_DLL_INTERFACE_VERSION 1

// Game DLL must export these functions with C linkage
// Use GAME_API_C when defining these in the game project

#ifdef __cplusplus
extern "C" {
#endif

// Called once when DLL is loaded
// Returns the interface version this DLL was compiled against
typedef int (*GameDLL_GetVersion_t)();

// Called to initialize game systems
// world: The ECS world to register components and systems with
typedef void (*GameDLL_Init_t)(World* world, std::map<Engine::ECS::Component::TypeID, Engine::ECS::Component::ComponentCreator> *compReg);

// Called every frame during play mode
// deltaTime: Time since last frame in seconds
typedef void (*GameDLL_Update_t)(float deltaTime);

// Called before DLL is unloaded (for cleanup)
typedef void (*GameDLL_Shutdown_t)();

// Called to register custom components
typedef void (*GameDLL_RegisterComponents_t)();

// Called to register custom systems
// world: The ECS world to register systems with
typedef void (*GameDLL_RegisterSystems_t)(World* world);

// Optional: Called before hot-reload to save custom state
// Returns a pointer to serialized state data (game manages memory)
typedef void* (*GameDLL_OnBeforeReload_t)(size_t* outSize);

// Optional: Called after hot-reload to restore custom state
// data: The state data from OnBeforeReload
// size: Size of the state data
typedef void (*GameDLL_OnAfterReload_t)(void* data, size_t size);

#ifdef __cplusplus
}
#endif

// Struct containing all function pointers for the game DLL
struct GameDLLFunctions {
    GameDLL_GetVersion_t GetVersion = nullptr;
    GameDLL_Init_t Init = nullptr;
    GameDLL_Update_t Update = nullptr;
    GameDLL_Shutdown_t Shutdown = nullptr;
    GameDLL_RegisterComponents_t RegisterComponents = nullptr;
    GameDLL_RegisterSystems_t RegisterSystems = nullptr;
    GameDLL_OnBeforeReload_t OnBeforeReload = nullptr;  // Optional
    GameDLL_OnAfterReload_t OnAfterReload = nullptr;    // Optional

    bool IsValid() const {
        // Required functions must be present
        return GetVersion != nullptr &&
               Init != nullptr &&
               Update != nullptr &&
               Shutdown != nullptr &&
               RegisterComponents != nullptr &&
               RegisterSystems != nullptr;
    }
};

// Macro to help implement the required exports in game DLLs
#define IMPLEMENT_GAME_DLL_EXPORTS() \
    GAME_API_C int GameDLL_GetVersion() { return GAME_DLL_INTERFACE_VERSION; } \
    GAME_API_C void GameDLL_Init(World* world); \
    GAME_API_C void GameDLL_Update(float deltaTime); \
    GAME_API_C void GameDLL_Shutdown(); \
    GAME_API_C void GameDLL_RegisterComponents(); \
    GAME_API_C void GameDLL_RegisterSystems(World* world);

#endif // ZUES_GAME_DLL_INTERFACE_H
