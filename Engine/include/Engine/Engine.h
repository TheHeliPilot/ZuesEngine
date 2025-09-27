//
// Created by bucka on 9/26/2025.
//

#ifndef ZUESENGINE_ENGINE_H
#define ZUESENGINE_ENGINE_H

#include "Log.h"
#include "Types.h"
#include "EngineDefines.h"
#include "EventSystem/EventSystem.h"
#include "EventSystem/Events.h"

namespace Engine {
    extern EventSystem* IEventSystem;
    void Initialize();
    void Update();
    void Shutdown();
}

#endif //ZUESENGINE_ENGINE_H