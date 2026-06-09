#pragma once
#include <zues/api.h>
#include <zues/types.h>

namespace Engine {

// Broadcast-only pub/sub bus. Use for window/file/hot-reload/input events.
// NEVER for per-frame per-entity updates — that's what services are for.
//
// Event types are POD structs. Raw API crosses DLL boundaries safely; a typed
// wrapper lives in events.inl (included when reflection is available).
class ZUES_API EventBus {
public:
    using RawHandler = void (*)(void* user, const void* event);

    EventBus();
    ~EventBus();

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    Result subscribe_raw(const char* event_id, RawHandler handler, void* user);
    void   publish_raw  (const char* event_id, const void* event, u32 size);

    // Currently synchronous (immediate dispatch). flush() is a no-op for now
    // but will matter when we add deferred/queued modes.
    void flush();

    // ---- Typed wrapper ------------------------------------------------------
    //
    // Events must be POD and define:
    //   static constexpr const char* EVENT_ID = "zues.something.unique";
    //
    // Handlers are non-capturing function pointers. State should live in
    // module-static data (or be looked up via the service registry) — passing
    // captures across the DLL boundary would require heap-allocating a closure
    // and inviting the cross-DLL crash class we're explicitly avoiding.
    //
    // Example:
    //   struct WindowResized {
    //       static constexpr const char* EVENT_ID = "zues.window_resized";
    //       int width, height;
    //   };
    //   bus->subscribe<WindowResized>([](const WindowResized& e) { ... });
    //   bus->publish(WindowResized{1920, 1080});

    template <typename E>
    Result subscribe(void (*handler)(const E&)) {
        auto thunk = +[](void* user, const void* event) {
            using FnPtr = void (*)(const E&);
            auto h = reinterpret_cast<FnPtr>(user);
            h(*static_cast<const E*>(event));
        };
        return subscribe_raw(E::EVENT_ID, thunk, reinterpret_cast<void*>(handler));
    }

    template <typename E>
    void publish(const E& event) {
        publish_raw(E::EVENT_ID, &event, static_cast<u32>(sizeof(E)));
    }

private:
    struct Impl;
    Impl* m_impl;
};

}  // namespace Engine
