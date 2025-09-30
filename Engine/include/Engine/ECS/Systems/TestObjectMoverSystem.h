// ViewportCameraSystem.h

#pragma once
#include "../../Input.h"
#include "../System.h"
#include "../Components.h"

class TestObjectMoverSystem final : public SystemBase<TransformComponent*, TestObjectMoverTag*> {
public:
    TestObjectMoverSystem();

    void Update(const float deltaTime, TransformComponent* pos, TestObjectMoverTag* vc_tag) override;
};