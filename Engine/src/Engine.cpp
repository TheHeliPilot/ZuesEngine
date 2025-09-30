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

    void Initialize(const Network::Role role, const std::string& address, const uint16_t port) {
        IEventSystem = new EventSystem();
        INetwork = new Network();

        //ECS Component Registrations, BEFORE FIRST CALL!
        Engine::ECS::Component::RegisterComponent<TransformComponent>();
        Engine::ECS::Component::RegisterComponent<SpriteComponent>();
        Engine::ECS::Component::RegisterComponent<CameraComponent>();
        Engine::ECS::Component::RegisterComponent<ViewportCameraTag>();

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

        Core::Init();

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
}