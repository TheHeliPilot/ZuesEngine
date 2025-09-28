#ifndef ZUESENGINE_CORE_H
#define ZUESENGINE_CORE_H

#include "ECS/World.h"
#include "ECS/Systems/Systems.h"
#include <memory>
#include <cstdint> // Required for uint32_t

namespace Engine {
    class Core {
    public:
        static void Init();
        // Accepts deltaTime for framerate-independent updates
        static void Update(float deltaTime);
        static void Shutdown();

    private:
        // Core Game/ECS State: The game's world is managed here
        static std::unique_ptr<World> s_World;

        // Helper to cleanly run all systems registered to the World
        static void RunSystems(float deltaTime);
    };
}

#endif //ZUESENGINE_CORE_H
