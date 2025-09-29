#pragma once

#include "EventSystem.h"
#include <string>
#include <utility>
#include <format>
#include "../EngineDefines.h"
#include <sstream>
#include <iomanip>

namespace Engine {

    class LogEvent final : public Event {
    public:
        std::string message;
        Log::EngineLog::LogLevel logLevel;

        explicit LogEvent(const Log::EngineLog::LogLevel level, std::string message)
            : message(std::move(message)), logLevel(level) { }

        // Required for templated subscription
        static EventType StaticType() { return EventType::LogE; }

        EventType GetEventType() const override {
            return StaticType();
        }

        std::string GetMessage() const {
            std::string logLevelName = "Info";
            if (logLevel == LOGLEVEL_WARN)
                logLevelName = "Warning";
            else if (logLevel == LOGLEVEL_ERR)
                logLevelName = "Error";

            std::time_t t = std::chrono::system_clock::to_time_t(timestamp);
            std::tm tm = *std::localtime(&t);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%H:%M:%S");
            return "|" + oss.str() +
                   "| " + logLevelName + ": " + message;
        }
    };

    // --- Keyboard Events ---

    class KeyEvent : public Event {
    protected:
        int keyCode;
    public:
        int GetKeyCode() const { return keyCode; }
        explicit KeyEvent(const int code) : keyCode(code) {}
        // No StaticType() here, as this is a base class
    };

    class KeyPressEvent final : public KeyEvent {
    public:
        explicit KeyPressEvent(const int code) : KeyEvent(code) {}
        static EventType StaticType() { return EventType::KeyPressE; }
        EventType GetEventType() const override { return StaticType(); }
    };

    class KeyReleaseEvent final : public KeyEvent {
    public:
        explicit KeyReleaseEvent(const int code) : KeyEvent(code) {}
        static EventType StaticType() { return EventType::KeyReleaseE; }
        EventType GetEventType() const override { return StaticType(); }
    };

    // --- Mouse Events ---

    class MouseMoveEvent final : public Event {
        double x, y;
    public:
        MouseMoveEvent(const double xPos, const double yPos) : x(xPos), y(yPos) {}
        static EventType StaticType() { return EventType::MouseMoveE; }
        EventType GetEventType() const override { return StaticType(); }
        double GetX() const { return x; }
        double GetY() const { return y; }
    };

    class MouseButtonPressEvent final : public Event {
        int button;
    public:
        explicit MouseButtonPressEvent(const int btn) : button(btn) {}
        static EventType StaticType() { return EventType::MouseButtonPressE; }
        EventType GetEventType() const override { return StaticType(); }
        int GetButton() const { return button; }
    };

    class MouseButtonReleaseEvent final : public Event {
        int button;
    public:
        explicit MouseButtonReleaseEvent(const int btn) : button(btn) {}
        static EventType StaticType() { return EventType::MouseButtonReleaseE; }
        EventType GetEventType() const override { return StaticType(); }
        int GetButton() const { return button; }
    };

} // namespace Engine
