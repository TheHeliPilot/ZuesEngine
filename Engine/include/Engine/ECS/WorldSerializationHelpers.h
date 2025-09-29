// C:/.../Engine/include/Engine/ECS/WorldSerializationHelpers.h

#pragma once

#include "../../json/json.hpp"
#include "Component.h"
#include "Entity.h"
#include "ECSConfig.h" // Needed for ComponentSignature (used indirectly via Component.h)

#include <tuple>
#include <string>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <utility>
#include <functional>
#include <map>
#include <memory>
#include <type_traits> // CRITICAL: For std::is_empty_v and std::enable_if_t

#include "../Math.h" // CRITICAL: Needed for Math::Vec2 and Math::Vec4 definitions

using json = nlohmann::json;

namespace Engine::Math {
    // Assuming Vec2 has public members x and y
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec2, x, y)

    // Assuming Vec4 has public members x, y, z, and w
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec4, x, y, z, w)
}

namespace Engine {

    // ----------------------------------------------------------------------
    // --- 1. JSON ADL Overloads for Custom Component Member Types (Fix) ---
    // ----------------------------------------------------------------------

    // Assuming Math::Vec2 has public float x, y members
    inline void to_json(json& j, const Math::Vec2& v) {
        j = {{"x", v.x}, {"y", v.y}};
    }

    inline void from_json(const json& j, Math::Vec2& v) {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
    }

    // Assuming Math::Vec4 has public float x, y, z, w members
    inline void to_json(json& j, const Math::Vec4& v) {
        j = {{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
    }

    inline void from_json(const json& j, Math::Vec4& v) {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
        v.z = j.at("z").get<float>();
        v.w = j.at("w").get<float>();
    }

    // ----------------------------------------------------------------------
    // --- 2. Type Trait for Automatic Empty Tag Detection (Automation) ---
    // ----------------------------------------------------------------------

    template <typename T>
    struct is_empty_tag : std::integral_constant<bool,
        // std::is_empty_v is the correct, standard way to detect a tag component.
        std::is_empty_v<T>
    > {};


    // ----------------------------------------------------------------------
    // --- 3. Generic Helper Implementations (Reflection Logic) ---
    // ----------------------------------------------------------------------

    // Helper function to serialize any data component struct T into a JSON object.
    template <typename T, std::size_t... Is>
    json ComponentToJsonImpl(const T& component, std::index_sequence<Is...>) {
        json j;
        // The const reference here is correct for serialization.
        const auto& tup = std::tie(component);
        ((j["m" + std::to_string(Is)] = std::get<Is>(tup)), ...);
        return j;
    }

    template <typename T, std::size_t... Is>
    void ComponentFromJsonImpl(T& component, const json& j, std::index_sequence<Is...>) {
        auto tup = std::tie(component);
        ((j.at("m" + std::to_string(Is)).get_to(std::get<Is>(tup))), ...);
    }


    // ----------------------------------------------------------------------
    // --- 4. Component Serialization Interface (SFINAE Dispatcher) ---
    // ----------------------------------------------------------------------

    // A. ComponentToJson: Empty Tag Implementation
    template <typename T>
    std::enable_if_t<is_empty_tag<T>::value, json>
    ComponentToJson(const T& component) {
        return json::object();
    }

    // B. ComponentToJson: Data Component (Reflection) Implementation
    template <typename T>
    std::enable_if_t<!is_empty_tag<T>::value, json>
    ComponentToJson(const T& component) {
        // Data component: dispatch to reflection implementation
        constexpr size_t N = std::tuple_size_v<decltype(std::tie(component))>;
        return ComponentToJsonImpl(component, std::make_index_sequence<N>{});
    }

    // C. ComponentFromJson: Empty Tag Implementation
    template <typename T>
    std::enable_if_t<is_empty_tag<T>::value, void>
    ComponentFromJson(T& component, const json& j) {
        // Tag component: do nothing (it's already default-constructed)
    }

    // D. ComponentFromJson: Data Component (Reflection) Implementation
    template <typename T>
    std::enable_if_t<!is_empty_tag<T>::value, void>
    ComponentFromJson(T& component, const json& j) {
        // Data component: dispatch to reflection implementation
        constexpr size_t N = std::tuple_size_v<decltype(std::tie(component))>;
        ComponentFromJsonImpl(component, j, std::make_index_sequence<N>{});
    }


    // ----------------------------------------------------------------------
    // --- 5. Serialization Data Structure Definitions (UNCHANGED) ---
    // ----------------------------------------------------------------------

    struct SerializedComponent {
        Engine::ECS::Component::TypeID typeID;
        json data;
    };

    // ... [SerializedEntity and WorldSnapshot structs and their to_json/from_json definitions] ...

    struct SerializedEntity {
        EntityID id;
        std::vector<SerializedComponent> components;
    };

    struct WorldSnapshot {
        int version = 1;
        std::vector<SerializedEntity> entities;
    };

    // nlohmann::json ADL Serialization Overloads for Engine types (EntityID, Serialized...)
    // ... [All inline to_json/from_json definitions for Engine::EntityID, SerializedComponent, SerializedEntity, WorldSnapshot] ...

    inline void to_json(json& j, const EntityID& id) { j = id.id; }
    inline void from_json(const json& j, EntityID& id) { id.id = j.get<uint64_t>(); }

    inline void to_json(json& j, const SerializedComponent& sc) {
        j["typeID"] = sc.typeID;
        j["data"] = sc.data;
    }

    inline void from_json(const json& j, SerializedComponent& sc) {
        j.at("typeID").get_to(sc.typeID);
        j.at("data").get_to(sc.data);
    }

    inline void to_json(json& j, const SerializedEntity& se) {
        j["id"] = se.id.id;
        j["components"] = se.components;
    }

    inline void from_json(const json& j, SerializedEntity& se) {
        j.at("id").get_to(se.id.id);
        j.at("components").get_to(se.components);
    }

    inline void to_json(json& j, const WorldSnapshot& ws) {
        j["version"] = ws.version;
        j["entities"] = ws.entities;
    }

    inline void from_json(const json& j, WorldSnapshot& ws) {
        if (j.contains("version")) {
            j.at("version").get_to(ws.version);
        }
        j.at("entities").get_to(ws.entities);
    }


    // ----------------------------------------------------------------------
    // --- 6. Component Registration Helper Types (THE REGISTRY) (UNCHANGED) ---
    // ----------------------------------------------------------------------

    struct IComponentSerializer {
        virtual ~IComponentSerializer() = default;
        virtual std::unique_ptr<IComponentArray> CreateComponentArray() const = 0;
        virtual void DeserializeAndAdd(IComponentArray* array, const json& data) const = 0;
        virtual json SerializeComponent(IComponentArray* array, size_t index) const = 0;
    };

    template <typename T>
    struct ComponentSerializer final : public IComponentSerializer {
        static_assert(std::is_default_constructible_v<T>, "ECS Component must be default constructible for serialization.");

        std::unique_ptr<IComponentArray> CreateComponentArray() const override {
            // Assumes ComponentArray<T> is defined in Component.h
            return std::make_unique<ComponentArray<T>>();
        }

        void DeserializeAndAdd(IComponentArray* array, const json& data) const override {
            ComponentArray<T>* specificArray = static_cast<ComponentArray<T>*>(array);
            T component{}; // Default construct
            // Dispatch to the correct ComponentFromJson (Tag or Data)
            ComponentFromJson(component, data);
            specificArray->data.push_back(std::move(component));
        }

        json SerializeComponent(IComponentArray* array, size_t index) const override {
            const ComponentArray<T>* specificArray = static_cast<const ComponentArray<T>*>(array);
            const T& component = specificArray->data.at(index);
            // Dispatch to the correct ComponentToJson (Tag or Data)
            return ComponentToJson(component);
        }
    };

    struct ComponentRegistry {
        std::map<Engine::ECS::Component::TypeID, std::unique_ptr<IComponentSerializer>> serializers;

        template<typename T>
        void RegisterComponent(const std::string& typeName) {
            Engine::ECS::Component::TypeID id = Engine::ECS::Component::GetTypeID<T>();
            if (serializers.find(id) != serializers.end()) return;
            // The ComponentSerializer<T> now correctly handles tags and data components
            serializers[id] = std::make_unique<ComponentSerializer<T>>();
        }

        IComponentSerializer* GetSerializer(const Engine::ECS::Component::TypeID id) const {
            auto it = serializers.find(id);
            if (it == serializers.end()) {
                throw std::runtime_error("Component TypeID not found in registry. Did you forget to register it?");
            }
            return it->second.get();
        }
    };
} // namespace Engine