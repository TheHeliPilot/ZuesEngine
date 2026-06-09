#include <zues/events.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

namespace {
    struct HandlerEntry {
        EventBus::RawHandler fn;
        void*                user;
    };
}

struct EventBus::Impl {
    std::unordered_map<std::string, std::vector<HandlerEntry>> subs;
};

EventBus::EventBus() : m_impl(new Impl) {}
EventBus::~EventBus() { delete m_impl; }

Result EventBus::subscribe_raw(const char* event_id, RawHandler handler, void* user) {
    if (!event_id || !handler) return Result::InvalidArgument;
    m_impl->subs[event_id].push_back({handler, user});
    return Result::Ok;
}

void EventBus::publish_raw(const char* event_id, const void* event, u32 /*size*/) {
    if (!event_id) return;
    auto it = m_impl->subs.find(event_id);
    if (it == m_impl->subs.end()) return;
    for (auto& h : it->second) h.fn(h.user, event);
}

void EventBus::flush() {
    // Synchronous dispatch for now — nothing queued.
}

}  // namespace Engine
