#pragma once
#include <cstddef>
#include <map>
#include <memory>
#include <typeindex>

// Utility function to get a unique ID for each component type at compile time
namespace Engine::ECS::Component {
    using TypeID = size_t;

    // Use a static counter to generate unique IDs
    inline TypeID GetNextID() {
        static TypeID lastID = 0;
        return lastID++;
    }

    // Template function to get the unique ID for a specific component type T
    template <typename T>
    inline TypeID GetTypeID() {
        static TypeID id = GetNextID();
        return id;
    }
}

// All components should be plain data structs.
// Example Component (Defined where you use it, not here)
/*
struct Position {
    float x = 0.0f;
    float y = 0.0f;
};
*/