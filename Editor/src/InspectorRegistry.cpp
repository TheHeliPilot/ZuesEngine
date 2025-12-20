#include "../include/customInspectors/InspectorRegistry.h"

std::unordered_map<int, std::unique_ptr<IComponentInspector>>
    InspectorRegistry::s_Inspectors;

void InspectorRegistry::Register(const int typeID, std::unique_ptr<IComponentInspector> inspector) {
    s_Inspectors[typeID] = std::move(inspector);
}

IComponentInspector* InspectorRegistry::Get(int typeID) {
    auto it = s_Inspectors.find(typeID);
    return it != s_Inspectors.end() ? it->second.get() : nullptr;
}

bool InspectorRegistry::Has(int typeID) {
    return s_Inspectors.contains(typeID);
}
