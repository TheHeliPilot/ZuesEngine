#pragma once

#include "../IComponentInspector.h"

class BoxColliderInspector final : public IComponentInspector {
public:
    bool OnGui(const char* label, nlohmann::json& j) override;
};

class CircleColliderInspector final : public IComponentInspector {
public:
    bool OnGui(const char* label, nlohmann::json& j) override;
};
