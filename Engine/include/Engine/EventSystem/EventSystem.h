//
// Created by bucka on 9/27/2025.
//

#pragma once

#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include "../EngineDefines.h"

namespace Engine {

    enum class EventType {
        None = 0,
        LogE
    };

    // ------------------------------
    // Base Event
    // ------------------------------
    class Event {
    public:
        Event() {
            timestamp = std::chrono::system_clock::now();
        }
        virtual ~Event() = default;
        virtual EventType GetEventType() const = 0;

        TIMESTAMP timestamp;
    };

    // ------------------------------
    // Event System
    // ------------------------------
    class EventSystem final {
    public:
        EventSystem() = default;
        virtual ~EventSystem() = default;

        // Type-safe subscribe
        template<typename TEvent>
        void Subscribe(std::function<void(const TEvent&)> func) {
            auto wrapper = [f = std::move(func)](const Event& e) {
                f(static_cast<const TEvent&>(e));
            };
            m_Listeners[TEvent::StaticType()].push_back(std::move(wrapper));
        }

        // Dispatch event
        void Dispatch(const Event& event) {
            EventType type = event.GetEventType();
            if (m_Listeners.contains(type)) {
                for (auto& listener : m_Listeners[type]) {
                    listener(event);
                }
            }
        }

    private:
        std::unordered_map<EventType, std::vector<std::function<void(const Event&)>>> m_Listeners;
    };

} // namespace Engine
