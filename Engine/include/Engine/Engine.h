//
// Created by bucka on 9/26/2025.
//

#ifndef ZUESENGINE_ENGINE_H
#define ZUESENGINE_ENGINE_H

#include "ZuesAPI.h"

#define GLFW_INCLUDE_NONE
#include "Core.h"
#include "Log.h"
#include "Types.h"
#include "EngineDefines.h"
#include "TextureManager.h"
#include "Network.h"
#include "EventSystem/EventSystem.h"
#include "EventSystem/Events.h"
#include "SceneSetup.h"
#include "ECS/Systems/Systems.h"
#include "HitTest.h"
#include "ECS/HierarchyOutliner.h"
#include "GameDLLInterface.h"
#include "../stb/stb_image.h"

namespace Engine {
    ZUES_API extern EventSystem* IEventSystem;
    ZUES_API extern Network* INetwork;

    ZUES_API void Initialize(bool enableNetwork, bool isHost, const std::string& address, uint16_t port, bool autoRegister = true);
    ZUES_API void Update(System::SystemRole currentMode);
    ZUES_API void Shutdown();

    ZUES_API void RegisterComponents();
    ZUES_API void RegisterSystems(World*);
    ZUES_API EventSystem* GetCurrentEventSystem();
}

#endif //ZUESENGINE_ENGINE_H
