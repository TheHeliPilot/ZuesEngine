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
        Engine::ECS::Component::RegisterComponent<PositionComponent>();
        Engine::ECS::Component::RegisterComponent<SpriteComponent>();
        Engine::ECS::Component::RegisterComponent<CameraComponent>();
        Engine::ECS::Component::RegisterComponent<ViewportCameraTag>();

        switch (role) {
            case Network::Role::Client:
                if (!INetwork->Connect(address, port)) {
                    ENGINE_LOG("Continuing in singleplayer mode!", LOGLEVEL_WARN);
                }
                break;
            case Network::Role::Host:
                if (INetwork->Host(address, port)) {
                    ENGINE_LOG("Continuing in singleplayer mode!", LOGLEVEL_WARN);
                }
                break;
            case Network::Role::None:
                ENGINE_LOG("No role selected for network initialization!", LOGLEVEL_WARN);
                break;
        }

        Core::Init();

        if (!glfwInit()) {
            ENGINE_LOG("Failed to initialize GLFW", LOGLEVEL_ERR);
        }else {
            Renderer::Init();
        }
    }

    float lastFrameTime = static_cast<float>(glfwGetTime());
    void Update() {
        INetwork->Update();

        const float currentTime = static_cast<float>(glfwGetTime());
        const float deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        Core::Update(deltaTime);
        //Renderer::Render();
    }

    void Shutdown() {
        Renderer::Shutdown();
        Core::Shutdown();
    }
}