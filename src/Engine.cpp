//
// Created by bucka on 9/26/2025.
//

#include "../include/Engine/Engine.h"
#include "../include/Engine/Core.h"
#include "../include/Engine/Renderer.h"
#include "../include/Engine/Network.h"

namespace Engine {
    void Initialize() {
        Core::Init();
        Renderer::Init();
        Network::Init();
    }

    void Update() {
        Core::Update();
        Renderer::Render();
        Network::Update();
    }

    void Shutdown() {
        Network::Shutdown();
        Renderer::Shutdown();
        Core::Shutdown();
    }
}