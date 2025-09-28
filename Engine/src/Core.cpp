#include "../include/Engine/Core.h"
#include "../include/Engine/Engine.h" // Needed for SetupSimpleScene
#include "../include/Engine/Renderer.h" // Needed for Renderer::Render
#include "../include/Engine/EngineDefines.h" // Needed for ENGINE_LOG
// Explicitly include systems for use with std::make_unique
#include "../include/Engine/ECS/Systems/CameraSystem.h" 
#include "../include/Engine/ECS/Systems/RenderingSystem.h"

// Initialize the static World pointer (must be defined here in Core.cpp)
std::unique_ptr<World> Engine::Core::s_World = nullptr; 

// NOTE: You must update include/Engine/Core.h to declare this static member:
// private: static std::unique_ptr<World> s_World;
// public: static void Init(uint32_t textureID);
// public: static void Update(float deltaTime);

void Engine::Core::Init() {
    // 1. Create the ECS World instance (Moved from main.cpp)
    s_World = std::make_unique<World>();
    ENGINE_LOG("Initializing Core Game Logic...", LOGLEVEL_INFO);

    // 2. Register Systems (Moved from main.cpp)
    s_World->RegisterSystem(std::make_unique<CameraSystem>());
    s_World->RegisterSystem(std::make_unique<RenderingSystem>());

    // 3. Setup Scene/Entities (Moved from main.cpp)
    Engine::SetupSimpleScene(s_World.get(), 0);

    s_World->UpdateSystems(0);

    ENGINE_LOG("Core Game Logic Initialized.", LOGLEVEL_INFO);
}

void Engine::Core::Update(const float deltaTime) {
    // CRITICAL: Run all ECS systems with the calculated deltaTime
    if (s_World) {
        s_World->UpdateSystems(deltaTime);
    }
}

void Engine::Core::Shutdown() {
    ENGINE_LOG("Shutting down Core Game Logic...", LOGLEVEL_INFO);
    s_World.reset(); // Safely destroy the world and all entities/systems
    ENGINE_LOG("Core Game Logic Shut down.", LOGLEVEL_INFO);
}