//
// Created by bucka on 9/26/2025.
//

#include "../include/Engine/Engine.h"
#include "../include/Engine/Core.h"
#include "../include/Engine/Renderer.h"
#include "../include/Engine/Network.h"
#include <GLFW/glfw3.h>

namespace Engine {

    EventSystem* IEventSystem = nullptr;
    Network* INetwork = nullptr;

    void Initialize(const Network::Role role, const std::string& address, const uint16_t port, bool autoRegister) {
        IEventSystem = new EventSystem();
        INetwork = new Network();

        if (autoRegister) {
            RegisterComponents();
        }

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

        Core::Init(autoRegister);

        if (!glfwInit()) {
            LOG_ERROR("Failed to initialize GLFW");
        }else {
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
        Core::Shutdown();
    }

    void RegisterComponents() {
        Engine::ECS::Component::RegisterComponent<Engine::ECS::Component::TransformComponent>();
        Engine::ECS::Component::RegisterComponent<Engine::ECS::Component::SpriteComponent>();
        Engine::ECS::Component::RegisterComponent<Engine::ECS::Component::CameraComponent>();
        Engine::ECS::Component::RegisterComponent<Engine::ECS::Component::MainCameraTag>();
        Engine::ECS::Component::RegisterComponent<Engine::ECS::Component::ViewportCameraTag>();
        Engine::ECS::Component::RegisterComponent<Engine::ECS::Component::TestObjectMoverTag>();
    }

    void RegisterSystems(World* s_World) {
        s_World->RegisterSystem(std::make_unique<HierarchySystem>());
        s_World->RegisterSystem(std::make_unique<CameraSystem>());
        s_World->RegisterSystem(std::make_unique<RenderingSystem>());
        s_World->RegisterSystem(std::make_unique<ViewportCameraSystem>());
        s_World->RegisterSystem(std::make_unique<TestObjectMoverSystem>());
        s_World->UpdateSystems(0, System::SystemRole::Shared);
    }
}
