#pragma once

#include "../../json/json.hpp"
#include "Component.h"
#include "Entity.h"
#include "ECSConfig.h"

#include <tuple>
#include <string>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <utility>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include "../ZuesMath.h"

using json = nlohmann::json;

namespace Engine::Math {
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec2, x, y)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3, x, y, z)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec4, x, y, z, w)
}

namespace Engine {
    inline void to_json(json& j, const Math::Vec2& v) {
        j = {{"x", v.x}, {"y", v.y}};
    }

    inline void from_json(const json& j, Math::Vec2& v) {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
    }

    inline void to_json(json& j, const Math::Vec4& v) {
        j = {{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
    }

    inline void from_json(const json& j, Math::Vec4& v) {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
        v.z = j.at("z").get<float>();
        v.w = j.at("w").get<float>();
    }

    template <typename T>
    struct is_empty_tag : std::integral_constant<bool, std::is_empty_v<T>> {};

    template <typename T, std::size_t... Is>
    json ComponentToJsonImpl(const T& component, std::index_sequence<Is...>) {
        json j;
        const auto& tup = std::tie(component);
        ((j["m" + std::to_string(Is)] = std::get<Is>(tup)), ...);
        return j;
    }

    template <typename T, std::size_t... Is>
    void ComponentFromJsonImpl(T& component, const json& j, std::index_sequence<Is...>) {
        auto tup = std::tie(component);
        ((j.at("m" + std::to_string(Is)).get_to(std::get<Is>(tup))), ...);
    }

    template <typename T>
    std::enable_if_t<is_empty_tag<T>::value, json>
    ComponentToJson(const T& component) {
        return json::object();
    }

    template <typename T>
    std::enable_if_t<!is_empty_tag<T>::value, json>
    ComponentToJson(const T& component) {
        constexpr size_t N = std::tuple_size_v<decltype(std::tie(component))>;
        return ComponentToJsonImpl(component, std::make_index_sequence<N>{});
    }

    template <typename T>
    std::enable_if_t<is_empty_tag<T>::value, void>
    ComponentFromJson(T& component, const json& j) {}

    template <typename T>
    std::enable_if_t<!is_empty_tag<T>::value, void>
    ComponentFromJson(T& component, const json& j) {
        constexpr size_t N = std::tuple_size_v<decltype(std::tie(component))>;
        ComponentFromJsonImpl(component, j, std::make_index_sequence<N>{});
    }

    struct SerializedComponent {
        ECS::Component::TypeID typeID;
        std::string typeName;
        json data;

        friend void to_json(nlohmann::json& j, const SerializedComponent& sc) {
            j = nlohmann::json{{"typeID", sc.typeID}, {"typeName", sc.typeName}, {"data", sc.data}};
        }

        friend void from_json(const nlohmann::json& j, SerializedComponent& sc) {
            j.at("typeID").get_to(sc.typeID);
            // Use value() to handle old save files that might not have typeName yet
            sc.typeName = j.value("typeName", "");
            j.at("data").get_to(sc.data);
        }
    };

    struct SerializedEntity {
        EntityID id;
        std::vector<SerializedComponent> components;
    };

    struct WorldSnapshot {
        int version = 1;
        std::vector<SerializedEntity> entities;
    };

    inline void to_json(json& j, const EntityID& id) {
        j["entityId"] = id.id;
        j["entityName"] = id.name;
    }

    inline void from_json(const json& j, EntityID& id) {
        j.at("entityId").get_to(id.id);
        id.name = j["entityName"].get<std::string>();
    }

    inline void to_json(json& j, const SerializedEntity& se) {
        to_json(j, se.id);
        j["components"] = se.components;
    }

    inline void from_json(const json& j, SerializedEntity& se) {
        j.at("entityId").get_to(se.id.id);
        j.at("entityName").get_to(se.id.name);
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

    // ======================================================================
    // COMPONENT SERIALIZER INTERFACE (WITH INSPECTOR SUPPORT)
    // ======================================================================

    struct IComponentSerializer {
        virtual ~IComponentSerializer() = default;
        virtual std::unique_ptr<IComponentArray> CreateComponentArray() const = 0;
        virtual void DeserializeAndAdd(IComponentArray* array, const json& data) const = 0;
        virtual json SerializeComponent(IComponentArray* array, size_t index) const = 0;

        // NEW: Inspector methods
        virtual json SerializeFromPointer(void* componentPtr) const = 0;
        virtual void DeserializeIntoPointer(void* componentPtr, const json& data) const = 0;
    };

    template <typename T>
    struct ComponentSerializer final : public IComponentSerializer {
        static_assert(std::is_default_constructible_v<T>, "ECS Component must be default constructible for serialization.");

        [[nodiscard]] std::unique_ptr<IComponentArray> CreateComponentArray() const override {
            return std::make_unique<ComponentArray<T>>();
        }

        void DeserializeAndAdd(IComponentArray* array, const json& data) const override {
            auto* specificArray = static_cast<ComponentArray<T>*>(array);
            T component{};

            if (!data.empty()) {
                ComponentFromJson(component, data);
            }

            specificArray->data.push_back(std::move(component));
        }

        json SerializeComponent(IComponentArray* array, size_t index) const override {
            const ComponentArray<T>* specificArray = static_cast<const ComponentArray<T>*>(array);
            const T& component = specificArray->data.at(index);
            return ComponentToJson(component);
        }

        // NEW: Inspector methods
        json SerializeFromPointer(void* componentPtr) const override {
            const T* component = static_cast<const T*>(componentPtr);
            return ComponentToJson(*component);
        }

        void DeserializeIntoPointer(void* componentPtr, const json& data) const override {
            T* component = static_cast<T*>(componentPtr);
            ComponentFromJson(*component, data);
        }
    };

    struct ComponentRegistry {
        std::map<ECS::Component::TypeID, std::unique_ptr<IComponentSerializer>> serializers;
        std::map<ECS::Component::TypeID, std::string> typeNames;

        // The watermark: IDs below this are Engine, IDs at or above are DLL
        uint32_t engineComponentCount = 0;

        // Call this once after the Engine registers Transform, Sprite, etc.
        void MarkEngineRegistrationComplete() {
            engineComponentCount = static_cast<uint32_t>(serializers.size());
        }

        // --- Helper Methods for Hot-Reload & DLLs ---
        std::map<Engine::ECS::Component::TypeID, std::unique_ptr<IComponentSerializer>>& GetSerializers() {
            return serializers;
        }

        bool HasName(const std::string& name) const {
            for (auto const& [id, n] : typeNames) {
                if (n == name) return true;
            }
            return false;
        }

        ECS::Component::TypeID GetIDByName(const std::string& name) const {
            for (auto const& [id, n] : typeNames) {
                if (n == name) return id;
            }
            return 0;
        }

        size_t GetTotalRegisteredCount() const {
            return serializers.size();
        }

        template<typename T>
        void AddSerializer(ECS::Component::TypeID id, const std::string& typeName) {
            serializers[id] = std::make_unique<ComponentSerializer<T>>();
            typeNames[id] = typeName;
        }

        template<typename T>
        void UpdateSerializer(ECS::Component::TypeID id, const std::string& typeName) {
            // Just replace the unique_ptr. This updates the function pointers
            // to the ones inside the newly loaded DLL.
            serializers[id] = std::make_unique<ComponentSerializer<T>>();
            typeNames[id] = typeName;
        }

        // --- Original Methods ---

        template<typename T>
        void RegisterComponent(const std::string& typeName) {
            const ECS::Component::TypeID id = ECS::Component::GetTypeID<T>();
            if (serializers.contains(id)) return;

            serializers[id] = std::make_unique<ComponentSerializer<T>>();
            typeNames[id] = typeName;
        }

        std::map<Engine::ECS::Component::TypeID, std::unique_ptr<IComponentSerializer>>& GetSerializersMutable() {
            return serializers;
        }

        IComponentSerializer* GetSerializer(const Engine::ECS::Component::TypeID id) const {
            const auto it = serializers.find(id);
            if (it == serializers.end()) {
                return nullptr;
            }
            return it->second.get();
        }

        const std::string& GetTypeName(ECS::Component::TypeID id) const {
            static std::string unknown = "Unknown Component";
            auto it = typeNames.find(id);
            return (it != typeNames.end()) ? it->second : unknown;
        }

        const std::map<ECS::Component::TypeID, std::unique_ptr<IComponentSerializer>>& GetAllSerializers() const {
            return serializers;
        }
    };
} // namespace Engine