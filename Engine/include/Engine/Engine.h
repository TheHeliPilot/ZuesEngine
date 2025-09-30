//
// Created by bucka on 9/26/2025.
//

#ifndef ZUESENGINE_ENGINE_H
#define ZUESENGINE_ENGINE_H

#define GLFW_INCLUDE_NONE
#include "Log.h"
#include "Types.h"
#include "EngineDefines.h"
#include "Network.h"
#include "EventSystem/EventSystem.h"
#include "EventSystem/Events.h"
#include "SceneSetup.h"
#include "ECS/Systems/Systems.h"
#include "ProjectManager.h"
#include "HitTest.h"
#include "../stb/stb_image.h"

namespace Engine {
    extern EventSystem* IEventSystem;
    extern Network* INetwork;

    void Initialize(Network::Role role, const std::string& address, const uint16_t port);
    void Update(System::SystemRole currentMode);
    void Shutdown();
}

#endif //ZUESENGINE_ENGINE_H