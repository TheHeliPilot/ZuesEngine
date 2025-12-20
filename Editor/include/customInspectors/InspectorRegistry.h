#pragma once

#include <unordered_map>
#include <memory>
#include "../IComponentInspector.h"

class InspectorRegistry {
public:
    static void Register(int typeID, std::unique_ptr<IComponentInspector> inspector);
    static IComponentInspector* Get(int typeID);
    static bool Has(int typeID);

private:
    static std::unordered_map<int, std::unique_ptr<IComponentInspector>> s_Inspectors;
};
