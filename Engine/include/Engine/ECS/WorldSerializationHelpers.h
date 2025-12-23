#pragma once

#include "../../json/json.hpp"
#include "Component.h"
#include "ECSConfig.h"
#include "Entity.h"
#include "System.h" // For System::SystemRole and System base class

#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../ZuesMath.h"

using json = nlohmann::json;

// ======================================================================
// TOLERANT JSON SERIALIZATION MACROS
// These macros handle schema evolution gracefully:
// - New fields in code get default values when loading old saves
// - Removed fields in code are silently ignored when loading
// - Use ZUES_COMPONENT_JSON instead of NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
// ======================================================================

// Helper to safely get a value with default fallback
#define ZUES_JSON_GET(j, field, obj) \
    if ((j).contains(#field)) { (j).at(#field).get_to((obj).field); }

// Main macro for component serialization - handles 1-10 fields
// Usage: ZUES_COMPONENT_JSON(MyComponent, field1, field2, field3)
#define ZUES_COMPONENT_JSON_1(Type, f1) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); }

#define ZUES_COMPONENT_JSON_2(Type, f1, f2) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}, {#f2, t.f2}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); ZUES_JSON_GET(j, f2, t); }

#define ZUES_COMPONENT_JSON_3(Type, f1, f2, f3) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}, {#f2, t.f2}, {#f3, t.f3}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); ZUES_JSON_GET(j, f2, t); ZUES_JSON_GET(j, f3, t); }

#define ZUES_COMPONENT_JSON_4(Type, f1, f2, f3, f4) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}, {#f2, t.f2}, {#f3, t.f3}, {#f4, t.f4}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); ZUES_JSON_GET(j, f2, t); ZUES_JSON_GET(j, f3, t); ZUES_JSON_GET(j, f4, t); }

#define ZUES_COMPONENT_JSON_5(Type, f1, f2, f3, f4, f5) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}, {#f2, t.f2}, {#f3, t.f3}, {#f4, t.f4}, {#f5, t.f5}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); ZUES_JSON_GET(j, f2, t); ZUES_JSON_GET(j, f3, t); ZUES_JSON_GET(j, f4, t); ZUES_JSON_GET(j, f5, t); }

#define ZUES_COMPONENT_JSON_6(Type, f1, f2, f3, f4, f5, f6) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}, {#f2, t.f2}, {#f3, t.f3}, {#f4, t.f4}, {#f5, t.f5}, {#f6, t.f6}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); ZUES_JSON_GET(j, f2, t); ZUES_JSON_GET(j, f3, t); ZUES_JSON_GET(j, f4, t); ZUES_JSON_GET(j, f5, t); ZUES_JSON_GET(j, f6, t); }

#define ZUES_COMPONENT_JSON_7(Type, f1, f2, f3, f4, f5, f6, f7) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}, {#f2, t.f2}, {#f3, t.f3}, {#f4, t.f4}, {#f5, t.f5}, {#f6, t.f6}, {#f7, t.f7}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); ZUES_JSON_GET(j, f2, t); ZUES_JSON_GET(j, f3, t); ZUES_JSON_GET(j, f4, t); ZUES_JSON_GET(j, f5, t); ZUES_JSON_GET(j, f6, t); ZUES_JSON_GET(j, f7, t); }

#define ZUES_COMPONENT_JSON_8(Type, f1, f2, f3, f4, f5, f6, f7, f8) \
    inline void to_json(nlohmann::json& j, const Type& t) { j = nlohmann::json{{#f1, t.f1}, {#f2, t.f2}, {#f3, t.f3}, {#f4, t.f4}, {#f5, t.f5}, {#f6, t.f6}, {#f7, t.f7}, {#f8, t.f8}}; } \
    inline void from_json(const nlohmann::json& j, Type& t) { t = Type{}; ZUES_JSON_GET(j, f1, t); ZUES_JSON_GET(j, f2, t); ZUES_JSON_GET(j, f3, t); ZUES_JSON_GET(j, f4, t); ZUES_JSON_GET(j, f5, t); ZUES_JSON_GET(j, f6, t); ZUES_JSON_GET(j, f7, t); ZUES_JSON_GET(j, f8, t); }

// Argument counting helper macros
#define ZUES_GET_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME
#define ZUES_COMPONENT_JSON(Type, ...) ZUES_GET_MACRO(__VA_ARGS__, \
    ZUES_COMPONENT_JSON_8, ZUES_COMPONENT_JSON_7, ZUES_COMPONENT_JSON_6, \
    ZUES_COMPONENT_JSON_5, ZUES_COMPONENT_JSON_4, ZUES_COMPONENT_JSON_3, \
    ZUES_COMPONENT_JSON_2, ZUES_COMPONENT_JSON_1)(Type, __VA_ARGS__)

namespace Engine::Math {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec2, x, y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec4, x, y, z, w)
} // namespace Engine::Math

namespace Engine {
inline void to_json(json &j, const Math::Vec2 &v) {
  j = {{"x", v.x}, {"y", v.y}};
}

inline void from_json(const json &j, Math::Vec2 &v) {
  v.x = j.at("x").get<float>();
  v.y = j.at("y").get<float>();
}

inline void to_json(json &j, const Math::Vec4 &v) {
  j = {{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
}

inline void from_json(const json &j, Math::Vec4 &v) {
  v.x = j.at("x").get<float>();
  v.y = j.at("y").get<float>();
  v.z = j.at("z").get<float>();
  v.w = j.at("w").get<float>();
}

template <typename T>
struct is_empty_tag : std::integral_constant<bool, std::is_empty_v<T>> {};

template <typename T, std::size_t... Is>
json ComponentToJsonImpl(const T &component, std::index_sequence<Is...>) {
  json j;
  const auto &tup = std::tie(component);
  ((j["m" + std::to_string(Is)] = std::get<Is>(tup)), ...);
  return j;
}

template <typename T, std::size_t... Is>
void ComponentFromJsonImpl(T &component, const json &j,
                           std::index_sequence<Is...>) {
  auto tup = std::tie(component);
  ((j.at("m" + std::to_string(Is)).get_to(std::get<Is>(tup))), ...);
}

template <typename T>
std::enable_if_t<is_empty_tag<T>::value, json>
ComponentToJson(const T &component) {
  return json::object();
}

template <typename T>
std::enable_if_t<!is_empty_tag<T>::value, json>
ComponentToJson(const T &component) {
  constexpr size_t N = std::tuple_size_v<decltype(std::tie(component))>;
  return ComponentToJsonImpl(component, std::make_index_sequence<N>{});
}

template <typename T>
std::enable_if_t<is_empty_tag<T>::value, void>
ComponentFromJson(T &component, const json &j) {}

template <typename T>
std::enable_if_t<!is_empty_tag<T>::value, void>
ComponentFromJson(T &component, const json &j) {
  constexpr size_t N = std::tuple_size_v<decltype(std::tie(component))>;
  ComponentFromJsonImpl(component, j, std::make_index_sequence<N>{});
}

struct SerializedComponent {
  ECS::Component::TypeID typeID;
  std::string typeName;
  json data;

  friend void to_json(nlohmann::json &j, const SerializedComponent &sc) {
    j = nlohmann::json{
        {"typeID", sc.typeID}, {"typeName", sc.typeName}, {"data", sc.data}};
  }

  friend void from_json(const nlohmann::json &j, SerializedComponent &sc) {
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

// Serialized system info for persistence
struct SerializedSystem {
  std::string systemName;
  bool isActive = true;

  friend void to_json(nlohmann::json &j, const SerializedSystem &ss) {
    j = nlohmann::json{{"systemName", ss.systemName},
                       {"isActive", ss.isActive}};
  }

  friend void from_json(const nlohmann::json &j, SerializedSystem &ss) {
    j.at("systemName").get_to(ss.systemName);
    ss.isActive = j.value("isActive", true);
  }
};

struct WorldSnapshot {
  int version = 1;
  std::vector<SerializedEntity> entities;
  std::vector<SerializedSystem>
      activeSystems; // Optional: tracks which systems are active
};

inline void to_json(json &j, const EntityID &id) {
  j["entityId"] = id.id;
  j["entityName"] = id.name;
}

inline void from_json(const json &j, EntityID &id) {
  j.at("entityId").get_to(id.id);
  id.name = j["entityName"].get<std::string>();
}

inline void to_json(json &j, const SerializedEntity &se) {
  to_json(j, se.id);
  j["components"] = se.components;
}

inline void from_json(const json &j, SerializedEntity &se) {
  j.at("entityId").get_to(se.id.id);
  j.at("entityName").get_to(se.id.name);
  j.at("components").get_to(se.components);
}

inline void to_json(json &j, const WorldSnapshot &ws) {
  j["version"] = ws.version;
  j["entities"] = ws.entities;
  if (!ws.activeSystems.empty()) {
    j["activeSystems"] = ws.activeSystems;
  }
}

inline void from_json(const json &j, WorldSnapshot &ws) {
  if (j.contains("version")) {
    j.at("version").get_to(ws.version);
  }
  j.at("entities").get_to(ws.entities);
  if (j.contains("activeSystems")) {
    j.at("activeSystems").get_to(ws.activeSystems);
  }
}

// ======================================================================
// COMPONENT SERIALIZER INTERFACE (WITH INSPECTOR SUPPORT)
// ======================================================================

struct IComponentSerializer {
  virtual ~IComponentSerializer() = default;
  virtual std::unique_ptr<IComponentArray> CreateComponentArray() const = 0;
  virtual void DeserializeAndAdd(IComponentArray *array,
                                 const json &data) const = 0;
  virtual json SerializeComponent(IComponentArray *array,
                                  size_t index) const = 0;

  // NEW: Inspector methods
  virtual json SerializeFromPointer(void *componentPtr) const = 0;
  virtual void DeserializeIntoPointer(void *componentPtr,
                                      const json &data) const = 0;
};

template <typename T>
struct ComponentSerializer final : public IComponentSerializer {
  static_assert(
      std::is_default_constructible_v<T>,
      "ECS Component must be default constructible for serialization.");

  [[nodiscard]] std::unique_ptr<IComponentArray>
  CreateComponentArray() const override {
    return std::make_unique<ComponentArray<T>>();
  }

  void DeserializeAndAdd(IComponentArray *array,
                         const json &data) const override {
    auto *specificArray = static_cast<ComponentArray<T> *>(array);
    T component{};

    if (!data.empty()) {
      // Call from_json directly using ADL
      from_json(data, component);
    }

    specificArray->data.push_back(std::move(component));
  }

  json SerializeComponent(IComponentArray *array, size_t index) const override {
    const ComponentArray<T> *specificArray =
        static_cast<const ComponentArray<T> *>(array);
    const T &component = specificArray->data.at(index);
    // Call to_json directly using ADL
    json j;
    to_json(j, component);
    return j;
  }

  // NEW: Inspector methods
  json SerializeFromPointer(void *componentPtr) const override {
    const T *component = static_cast<const T *>(componentPtr);
    // Call to_json directly using ADL
    json j;
    to_json(j, *component);
    return j;
  }

  void DeserializeIntoPointer(void *componentPtr,
                              const json &data) const override {
    T *component = static_cast<T *>(componentPtr);
    // Call from_json directly using ADL
    from_json(data, *component);
  }
};

struct ComponentRegistry {
  std::map<ECS::Component::TypeID, std::unique_ptr<IComponentSerializer>>
      serializers;
  std::map<ECS::Component::TypeID, std::string> typeNames;

  // The watermark: IDs below this are Engine, IDs at or above are DLL
  uint32_t engineComponentCount = 0;

  // Call this once after the Engine registers Transform, Sprite, etc.
  void MarkEngineRegistrationComplete() {
    engineComponentCount = static_cast<uint32_t>(serializers.size());
  }

  // --- Helper Methods for Hot-Reload & DLLs ---
  std::map<Engine::ECS::Component::TypeID,
           std::unique_ptr<IComponentSerializer>> &
  GetSerializers() {
    return serializers;
  }

  bool HasName(const std::string &name) const {
    for (auto const &[id, n] : typeNames) {
      if (n == name)
        return true;
    }
    return false;
  }

  ECS::Component::TypeID GetIDByName(const std::string &name) const {
    for (auto const &[id, n] : typeNames) {
      if (n == name)
        return id;
    }
    return 0;
  }

  size_t GetTotalRegisteredCount() const { return serializers.size(); }

  template <typename T>
  void AddSerializer(ECS::Component::TypeID id, const std::string &typeName) {
    serializers[id] = std::make_unique<ComponentSerializer<T>>();
    typeNames[id] = typeName;
  }

  template <typename T>
  void UpdateSerializer(ECS::Component::TypeID id,
                        const std::string &typeName) {
    // Just replace the unique_ptr. This updates the function pointers
    // to the ones inside the newly loaded DLL.
    serializers[id] = std::make_unique<ComponentSerializer<T>>();
    typeNames[id] = typeName;
  }

  // --- Original Methods ---

  template <typename T> void RegisterComponent(const std::string &typeName) {
    const ECS::Component::TypeID id = ECS::Component::GetTypeID<T>();
    if (serializers.contains(id))
      return;

    serializers[id] = std::make_unique<ComponentSerializer<T>>();
    typeNames[id] = typeName;
  }

  std::map<Engine::ECS::Component::TypeID,
           std::unique_ptr<IComponentSerializer>> &
  GetSerializersMutable() {
    return serializers;
  }

  IComponentSerializer *
  GetSerializer(const Engine::ECS::Component::TypeID id) const {
    const auto it = serializers.find(id);
    if (it == serializers.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  const std::string &GetTypeName(ECS::Component::TypeID id) const {
    static std::string unknown = "Unknown Component";
    auto it = typeNames.find(id);
    return (it != typeNames.end()) ? it->second : unknown;
  }

  const std::map<ECS::Component::TypeID,
                 std::unique_ptr<IComponentSerializer>> &
  GetAllSerializers() const {
    return serializers;
  }
};

// ======================================================================
// SYSTEM REGISTRY - Tracks available systems for the UI and hot-reload
// ======================================================================

// System creator function type - used to instantiate systems
// Note: System class is in global namespace, not Engine namespace
using SystemCreator = std::function<std::unique_ptr<::System>()>;

struct SystemInfo {
  std::string name;
  SystemCreator creator;
  ::System::SystemRole role = ::System::SystemRole::Game;
  bool isRequired = false; // Required systems cannot be removed
  bool isFromDLL = false;  // True if this system came from the game DLL
};

struct SystemRegistry {
  std::map<std::string, SystemInfo> registeredSystems;

  // The watermark: systems registered before this are engine systems
  size_t engineSystemCount = 0;

  // Call after registering engine systems
  void MarkEngineRegistrationComplete() {
    engineSystemCount = registeredSystems.size();
  }

  // Register a new system (with creator function)
  template <typename T>
  void RegisterSystem(const std::string &name, ::System::SystemRole role,
                      bool isRequired = false, bool isFromDLL = false) {
    SystemInfo info;
    info.name = name;
    info.role = role;
    info.isRequired = isRequired;
    info.isFromDLL = isFromDLL;
    info.creator = []() -> std::unique_ptr<::System> {
      auto sys = std::make_unique<T>();
      return sys;
    };
    registeredSystems[name] = std::move(info);
  }

  // Check if a system is registered by name
  bool HasSystem(const std::string &name) const {
    return registeredSystems.contains(name);
  }

  // Get system info by name
  const SystemInfo *GetSystemInfo(const std::string &name) const {
    auto it = registeredSystems.find(name);
    return (it != registeredSystems.end()) ? &it->second : nullptr;
  }

  // Create a system instance by name
  std::unique_ptr<::System> CreateSystem(const std::string &name) const {
    auto it = registeredSystems.find(name);
    if (it != registeredSystems.end() && it->second.creator) {
      auto sys = it->second.creator();
      if (sys) {
        sys->systemName = name;
        sys->isRequired = it->second.isRequired;
        sys->role = it->second.role;
      }
      return sys;
    }
    return nullptr;
  }

  // Get all registered systems (for UI)
  const std::map<std::string, SystemInfo> &GetAllSystems() const {
    return registeredSystems;
  }

  // Clear DLL-registered systems (for hot-reload)
  void ClearDLLSystems() {
    for (auto it = registeredSystems.begin(); it != registeredSystems.end();) {
      if (it->second.isFromDLL) {
        it = registeredSystems.erase(it);
      } else {
        ++it;
      }
    }
  }
};
} // namespace Engine