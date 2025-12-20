#pragma once

#include "Engine.h"

class IComponentInspector {
public:
    virtual ~IComponentInspector() = default;

    // label = component name
    // j     = serialized component data
    // return true if data was modified
    virtual bool OnGui(const char* label, nlohmann::json& j) = 0;
};
