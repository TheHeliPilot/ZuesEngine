#pragma once

#include "../IComponentInspector.h"

class RigidbodyInspector final : public IComponentInspector {
public:
    bool OnGui(const char* label, nlohmann::json& j) override;
};
