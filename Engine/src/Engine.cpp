//
// Created by bucka on 9/26/2025.
//

#include "../include/Engine/Engine.h"
#include "../include/Engine/Core.h"
#include "../include/Engine/Renderer.h"
#include "../include/Engine/Network.h"
#include "../include/Engine/ECS/Systems/Systems.h"
#include <GLFW/glfw3.h>

namespace Engine {

    // Global subsystem pointers - MUST be exported for game DLLs
    ZUES_API EventSystem* IEventSystem = nullptr;
    ZUES_API Network* INetwork = nullptr;

    void Initialize(bool enableNetwork, const bool isHost, const std::string& address, const uint16_t port, const bool autoRegister) {
        IEventSystem = new EventSystem();
        INetwork = new Network();

        Core::Init(autoRegister);

        // Initialize ENet system if networking is enabled
        if (enableNetwork) {
            if (!Network::InitializeSystem()) {
                LOG_ERROR("Failed to initialize network system!");
                enableNetwork = false; // Fall back to singleplayer
            }
        }

        if (enableNetwork) {
            if (isHost) {
                if (!INetwork->StartHost(address, port)) {
                    LOG_WARN("Failed to host - continuing in singleplayer mode!");
                }
            } else {
                if (!INetwork->StartClient(address, port)) {
                    LOG_WARN("Failed to connect - continuing in singleplayer mode!");
                }
            }
        } else {
            LOG_INFO("Starting in singleplayer mode (no networking)");
        }

        // Note: GLFW and Renderer are initialized by the Editor/Host application
        // The Editor calls gladLoadGLLoader and Renderer::Init after creating the OpenGL context
    }

    float lastFrameTime = static_cast<float>(glfwGetTime());
    void Update(const System::SystemRole currentMode) {
        // Poll network events every frame
        if (INetwork) {
            INetwork->PollEvents();
        }

        const auto currentTime = static_cast<float>(glfwGetTime());
        const float deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        Core::Update(deltaTime, currentMode);
        //Renderer::Render();
    }

    void Shutdown() {
        Renderer::Shutdown();
        TextureManager::Shutdown();
        Core::Shutdown();

        // Clean up network
        if (INetwork) {
            INetwork->Stop();
            delete INetwork;
            INetwork = nullptr;
        }

        Network::ShutdownSystem();

        if (IEventSystem) {
            delete IEventSystem;
            IEventSystem = nullptr;
        }
    }

    void RegisterComponents() {
        World* world = Core::GetCurrentWorld();
        if (!world) {
            LOG_ERROR("Cannot register components: No active world!");
            return;
        }

        // Register engine components with human-readable names
        Engine::ECS::Component::RegisterComponent<ECS::Component::TransformComponent>();
        world->RegisterComponent<ECS::Component::TransformComponent>("Transform");
        Engine::ECS::Component::RegisterComponent<ECS::Component::SpriteComponent>();
        world->RegisterComponent<ECS::Component::SpriteComponent>("Sprite");
        Engine::ECS::Component::RegisterComponent<ECS::Component::CameraComponent>();
        world->RegisterComponent<ECS::Component::CameraComponent>("Camera");
        Engine::ECS::Component::RegisterComponent<ECS::Component::MainCameraTag>();
        world->RegisterComponent<ECS::Component::MainCameraTag>("Main Camera Tag");
        Engine::ECS::Component::RegisterComponent<ECS::Component::ViewportCameraTag>();
        world->RegisterComponent<ECS::Component::ViewportCameraTag>("Viewport Camera");
        Engine::ECS::Component::RegisterComponent<ECS::Component::TestObjectMoverTag>();
        world->RegisterComponent<ECS::Component::TestObjectMoverTag>("Test Mover");
        Engine::ECS::Component::RegisterComponent<ECS::Component::TextComponent>();
        world->RegisterComponent<ECS::Component::TextComponent>("Text");
        Engine::ECS::Component::RegisterComponent<ECS::Component::RigidbodyComponent>();
        world->RegisterComponent<ECS::Component::RigidbodyComponent>("Rigidbody");
        Engine::ECS::Component::RegisterComponent<ECS::Component::BoxColliderComponent>();
        world->RegisterComponent<ECS::Component::BoxColliderComponent>("Box Collider");
        Engine::ECS::Component::RegisterComponent<ECS::Component::CircleColliderComponent>();
        world->RegisterComponent<ECS::Component::CircleColliderComponent>("Circle Collider");

        world->GetComponentRegistry().MarkEngineRegistrationComplete();

        LOG_INFO("Engine components registered successfully");
    }

    // Helper to create a system with proper metadata and add to registry
    template<typename T>
    static void RegisterSystemWithMetadata(World* world, const std::string& name, System::SystemRole role, bool isRequired) {
        // Register in the system registry for UI/serialization
        world->GetSystemRegistry().RegisterSystem<T>(name, role, isRequired, false);

        // Create and add the actual system instance
        auto system = std::make_unique<T>();
        system->systemName = name;
        system->role = role;
        system->isRequired = isRequired;
        world->RegisterSystem(std::move(system));
    }

    void RegisterSystems(World* s_World) {
        // Required engine systems (cannot be disabled)
        RegisterSystemWithMetadata<HierarchySystem>(s_World, "Hierarchy", System::SystemRole::Shared, true);
        RegisterSystemWithMetadata<PhysicsSystem>(s_World, "Physics", System::SystemRole::Game, true);
        RegisterSystemWithMetadata<CameraSystem>(s_World, "Camera", System::SystemRole::Shared, true);
        RegisterSystemWithMetadata<RenderingSystem>(s_World, "Rendering", System::SystemRole::Shared, true);
        RegisterSystemWithMetadata<TextRenderingSystem>(s_World, "Text Rendering", System::SystemRole::Shared, true);

        // Optional engine systems (can be disabled)
        RegisterSystemWithMetadata<TestObjectMoverSystem>(s_World, "Test Object Mover", System::SystemRole::Game, false);

        // Mark engine systems as complete
        s_World->GetSystemRegistry().MarkEngineRegistrationComplete();

        s_World->UpdateSystems(0, System::SystemRole::Shared);
    }

    EventSystem* GetCurrentEventSystem()  {
        return IEventSystem;
    }
}
