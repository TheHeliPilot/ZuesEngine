#pragma once

#include "Engine.h"

class InspectorUI {
public:
    static void InspectorWindow();

private:
    static bool DrawJsonComponentEditor(
        const char* name,
        nlohmann::json& j,
        int componentTypeID
    );

    static bool DrawJsonField(
        const char* label,
        nlohmann::json& value
    );
};
