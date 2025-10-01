#ifndef ZUESENGINE_CORE_H
#define ZUESENGINE_CORE_H

#include "ECS/World.h"
#include "ECS/Systems/Systems.h"
#include <memory>
#include <cstdint> // Required for uint32_t

namespace Engine {
    class Core {
    public:
        static void Init(bool autoRegister);
        // Accepts deltaTime for framerate-independent updates
        static void Update(float deltaTime, System::SystemRole currentMode);
        static void Shutdown();
        static bool SaveWorld(const std::string& filePath);
        static bool LoadWorld(const std::string& filePath);

        static bool AutoGenerateComponent(
            const std::string& componentName,
            const std::filesystem::path& headerDirectory
        );
        static bool AutoGenerateSystem(
            const std::string& systemName,
            const std::filesystem::path& headerDirectory,
            const std::filesystem::path& sourceDirectory
        );

        static World* GetCurrentWorld() {
            return s_World.get();
        }

    private:
        // Core Game/ECS State: The game's world is managed here
        static std::unique_ptr<World> s_World;

        // Helper to cleanly run all systems registered to the World
        static void RunSystems(float deltaTime);
    };
}

#endif //ZUESENGINE_CORE_H
