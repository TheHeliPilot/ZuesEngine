// ViewportCameraSystem.h

#pragma once
#include "../../Input.h"
#include "../System.h"
#include "../Components.h"

class TestObjectMoverSystem final : public SystemBase<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::TestObjectMoverTag*> {
public:
    TestObjectMoverSystem();

    void Update(const float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::TestObjectMoverTag* vc_tag) override;
};