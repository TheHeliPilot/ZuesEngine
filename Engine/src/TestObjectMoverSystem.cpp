//
// Created by bucka on 9/30/2025.
//

#include "../include/Engine/ECS/Systems/TestObjectMoverSystem.h"

TestObjectMoverSystem::TestObjectMoverSystem() {
    role = SystemRole::Editor;
}

void TestObjectMoverSystem::Update(const float deltaTime, Engine::ECS::Component::TransformComponent *pos, Engine::ECS::Component::TestObjectMoverTag *vc_tag) {
    pos->worldPosition.x = sin(10*deltaTime);
    pos->worldPosition.y = cos(deltaTime);
}
