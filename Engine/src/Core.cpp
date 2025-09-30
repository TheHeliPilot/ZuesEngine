#include "../include/Engine/Core.h"
#include "../include/Engine/Engine.h" // Needed for SetupSimpleScene
#include "../include/Engine/Renderer.h" // Needed for Renderer::Render
#include "../include/Engine/EngineDefines.h" // Needed for ENGINE_LOG
// Explicitly include systems for use with std::make_unique
#include "../include/Engine/ECS/Systems/CameraSystem.h"
#include "../include/Engine/ECS/Systems/HierarchySystem.h"
#include "../include/Engine/ECS/Systems/RenderingSystem.h"
#include "../include/Engine/ECS/Systems/TestObjectMoverSystem.h"
#include "../include/Engine/ECS/Systems/ViewportCameraSystem.h"

// Initialize the static World pointer (must be defined here in Core.cpp)
std::unique_ptr<World> Engine::Core::s_World = nullptr; 

// NOTE: You must update include/Engine/Core.h to declare this static member:
// private: static std::unique_ptr<World> s_World;
// public: static void Init(uint32_t textureID);
// public: static void Update(float deltaTime);

void Engine::Core::Init() {
    // 1. Create the ECS World instance (Moved from main.cpp)
    s_World = std::make_unique<World>();
    LOG_INFO("Initializing Core Game Logic...");

    // 2. Register Systems (Moved from main.cpp)
    s_World->RegisterSystem(std::make_unique<HierarchySystem>());
    s_World->RegisterSystem(std::make_unique<CameraSystem>());
    s_World->RegisterSystem(std::make_unique<RenderingSystem>());
    s_World->RegisterSystem(std::make_unique<ViewportCameraSystem>());
    s_World->RegisterSystem(std::make_unique<TestObjectMoverSystem>());

    // 3. Setup Scene/Entities (Moved from main.cpp)
    Engine::SetupSimpleScene(s_World.get(), 0);

    s_World->UpdateSystems(0, System::SystemRole::Shared);

    LOG_INFO("Core Game Logic Initialized.");
}

void Engine::Core::Update(const float deltaTime, const System::SystemRole currentMode) {
    // CRITICAL: Run all ECS systems with the calculated deltaTime
    if (s_World) {
        s_World->UpdateSystems(deltaTime, currentMode);
    }
}

void Engine::Core::Shutdown() {
    LOG_INFO("Shutting down Core Game Logic...");
    s_World.reset(); // Safely destroy the world and all entities/systems
    LOG_INFO("Core Game Logic Shut down.");
}

bool Engine::Core::SaveWorld(const std::string &filePath) {
    if (!s_World) {
        LOG_ERROR("Cannot save, no world exists!");
        return false;
    }

    return s_World->SaveToJson(filePath+"World.json");
}

bool Engine::Core::LoadWorld(const std::string &filePath) {
    return s_World->LoadFromJson(filePath);
}
