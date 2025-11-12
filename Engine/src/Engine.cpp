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

    EventSystem* IEventSystem = nullptr;
    Network* INetwork = nullptr;

    void Initialize(const Network::Role role, const std::string& address, const uint16_t port, bool autoRegister) {
        IEventSystem = new EventSystem();
        INetwork = new Network();

        Core::Init(autoRegister);

        switch (role) {
            case Network::Role::Client:
                if (!INetwork->Connect(address, port)) {
                    LOG_WARN("Continuing in singleplayer mode!");
                }
                break;
            case Network::Role::Host:
                if (INetwork->Host(address, port)) {
                    LOG_WARN("Continuing in singleplayer mode!");
                }
                break;
            case Network::Role::None:
                LOG_WARN("No role selected for network initialization!");
                break;
        }

        if (!glfwInit()) {
            LOG_ERROR("Failed to initialize GLFW");
        } else {
            Renderer::Init();
        }
    }

    float lastFrameTime = static_cast<float>(glfwGetTime());
    void Update(const System::SystemRole currentMode) {
        INetwork->Update();

        const float currentTime = static_cast<float>(glfwGetTime());
        const float deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        Core::Update(deltaTime, currentMode);
        //Renderer::Render();
    }

    void Shutdown() {
        Renderer::Shutdown();
        TextureManager::Shutdown();
        Core::Shutdown();
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

        LOG_INFO("Engine components registered successfully");
    }

    void RegisterSystems(World* s_World) {
        s_World->RegisterSystem(std::make_unique<HierarchySystem>());
        s_World->RegisterSystem(std::make_unique<PhysicsSystem>());
        s_World->RegisterSystem(std::make_unique<CameraSystem>());
        s_World->RegisterSystem(std::make_unique<RenderingSystem>());
        s_World->RegisterSystem(std::make_unique<TextRenderingSystem>());
        s_World->RegisterSystem(std::make_unique<ViewportCameraSystem>());
        s_World->RegisterSystem(std::make_unique<TestObjectMoverSystem>());
        s_World->UpdateSystems(0, System::SystemRole::Shared);
    }
}