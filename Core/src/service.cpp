#include <zues/service.h>

#include <string>
#include <unordered_map>

namespace Engine {

namespace {
    struct Entry {
        u32   version;
        void* vtable;
    };
}

struct ServiceRegistry::Impl {
    std::unordered_map<std::string, Entry> by_id;
};

ServiceRegistry::ServiceRegistry() : m_impl(new Impl) {}
ServiceRegistry::~ServiceRegistry() { delete m_impl; }

Result ServiceRegistry::register_service(const char* id, u32 version, void* vtable) {
    if (!id || !vtable) return Result::InvalidArgument;
    if (m_impl->by_id.contains(id)) return Result::AlreadyExists;
    m_impl->by_id.emplace(id, Entry{version, vtable});
    return Result::Ok;
}

void* ServiceRegistry::get_service(const char* id, u32 version) const {
    if (!id) return nullptr;
    auto it = m_impl->by_id.find(id);
    if (it == m_impl->by_id.end()) return nullptr;
    // Caller's minimum version: we provided must be >= requested.
    if (it->second.version < version) return nullptr;
    return it->second.vtable;
}

}  // namespace Engine
