#pragma once
#include "ECSConfig.h"
#include <vector>
#include <algorithm>
#include <memory>
#include <stdexcept>

// Utility function to get a unique ID for each component type at compile time
namespace Engine::ECS::Component {
    using TypeID = size_t;

    // Use a static counter to generate unique IDs
    inline TypeID GetNextID() {
        static TypeID lastID = 0;
        // Check for overflow against MAX_COMPONENTS
        if (lastID >= MAX_COMPONENTS) {
            throw std::runtime_error("Exceeded MAX_COMPONENTS limit. Increase MAX_COMPONENTS in ECSConfig.h.");
        }
        return lastID++;
    }

    // Template function to get the unique ID for a specific component type T
    template <typename T>
    inline TypeID GetTypeID() {
        static TypeID id = GetNextID();
        return id;
    }


    // All components should be plain data structs.
    // NOTE: Components must be registered via World::RegisterComponent<T>() before use.
}


// Component Storage Interface (Base class for type erasure)
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    // Performs Swap-and-Pop on the component at 'index'
    virtual void RemoveComponent(size_t index) = 0;
    // Copies/Moves a component from 'other' array at 'index' to this array's back
    virtual void AddComponentFrom(IComponentArray* other, size_t index) = 0;
    // Adds a default-constructed component to the back of the array
    virtual void AddDefaultComponent() = 0;
    // Returns a void* to the element at 'index'
    virtual void* GetVoidPtr(size_t index) = 0;
};

// Templated Component Storage (The contiguous arrays)
template<typename T>
class ComponentArray : public IComponentArray {
public:
    // The raw data vector (must remain public for serialization helpers)
    std::vector<T> data;

    // FIX 1: Add Get() method for assignment in World::AddComponent
    T& Get(size_t index) {
        if (index >= data.size()) {
            throw std::out_of_range("Component index out of bounds on Get().");
        }
        return data[index];
    }

    // FIX 2: Add GetData() for direct pointer access by RenderingSystem
    T* GetData() {
        return data.data();
    }

    void RemoveComponent(size_t index) override {
        // Swap-and-Pop: Copy the last element over the one being removed
        // O(1) removal, but destroys iteration order.
        data[index] = std::move(data.back());
        data.pop_back();
    }

    void AddComponentFrom(IComponentArray* other, size_t index) override {
        // Cast the generic IComponentArray back to the specific ComponentArray<T>
        ComponentArray<T>* otherArray = static_cast<ComponentArray<T>*>(other);
        // Move the component from the old array into the new array
        data.push_back(std::move(otherArray->data[index]));
    }

    void AddDefaultComponent() override {
        data.emplace_back();
    }

    void* GetVoidPtr(size_t index) override {
        if (index >= data.size()) {
            throw std::out_of_range("Component index out of bounds.");
        }
        return static_cast<void*>(&data[index]);
    }
};