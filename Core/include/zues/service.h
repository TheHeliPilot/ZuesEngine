#pragma once
#include <zues/api.h>
#include <zues/types.h>

namespace Engine {

// Central registry for typed services. Modules register their service vtables
// under a string ID; other modules look them up by the same ID.
//
// Service interfaces are defined as C structs-of-function-pointers in shared
// headers (e.g. Engine/services/renderer_2d.h). Each interface struct MUST
// define:
//   static constexpr const char* SERVICE_ID      = "Engine.xxx";
//   static constexpr u32         SERVICE_VERSION = 1;
// The templated get<T>() helper uses those to do a typed lookup.
class ZUES_API ServiceRegistry {
public:
    ServiceRegistry();
    ~ServiceRegistry();

    ServiceRegistry(const ServiceRegistry&)            = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    Result register_service(const char* id, u32 version, void* vtable);
    void*  get_service(const char* id, u32 version) const;

    template <typename Interface>
    Interface* get() const {
        return static_cast<Interface*>(get_service(Interface::SERVICE_ID, Interface::SERVICE_VERSION));
    }

private:
    struct Impl;
    Impl* m_impl;
};

}  // namespace Engine
