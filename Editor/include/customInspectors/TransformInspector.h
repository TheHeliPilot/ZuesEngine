#pragma once

#include "../IComponentInspector.h"

class TransformInspector final : public IComponentInspector {
public:
    bool OnGui(const char* label, nlohmann::json& j) override;
};
