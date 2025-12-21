// ViewportCameraSystem.h

#pragma once

#include "../../ZuesAPI.h"
#include "../../Input.h"
#include "../System.h"
#include "../Components.h"

class ZUES_API TestObjectMoverSystem final : public SystemBase<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::TestObjectMoverTag*> {
public:
    TestObjectMoverSystem();

    void Update(const float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::TestObjectMoverTag* vc_tag) override;
};