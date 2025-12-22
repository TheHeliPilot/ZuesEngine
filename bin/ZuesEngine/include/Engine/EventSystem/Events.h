#pragma once

#include "../ZuesAPI.h"

#include "EventSystem.h"
#include <string>
#include <utility>
#include <format>
#include "../EngineDefines.h"
#include <sstream>
#include <iomanip>

namespace Engine {

    class ZUES_API LogEvent final : public Event {
    public:
        std::string message;
        Log::EngineLog::LogLevel logLevel;
        std::string file;
        int line;

        explicit LogEvent(const Log::EngineLog::LogLevel level, std::string message, std::string file, const int line)
            : message(std::move(message)), logLevel(level), file(std::move(file)), line(line) {
        }

        // Required for templated subscription
        static EventType StaticType() { return EventType::LogE; }

        [[nodiscard]] EventType GetEventType() const override {
            return StaticType();
        }

        [[nodiscard]] std::string GetMessage() const {
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

    class ZUES_API KeyEvent : public Event {
    protected:
        int keyCode;
    public:
        [[nodiscard]] int GetKeyCode() const { return keyCode; }
        explicit KeyEvent(const int code) : keyCode(code) {}
        // No StaticType() here, as this is a base class
    };

    class ZUES_API KeyPressEvent final : public KeyEvent {
    public:
        explicit KeyPressEvent(const int code) : KeyEvent(code) {}
        static EventType StaticType() { return EventType::KeyPressE; }
        [[nodiscard]] EventType GetEventType() const override { return StaticType(); }
    };

    class ZUES_API KeyReleaseEvent final : public KeyEvent {
    public:
        explicit KeyReleaseEvent(const int code) : KeyEvent(code) {}
        static EventType StaticType() { return EventType::KeyReleaseE; }
        [[nodiscard]] EventType GetEventType() const override { return StaticType(); }
    };

    // --- Mouse Events ---

    class ZUES_API MouseMoveEvent final : public Event {
        double x, y;
    public:
        MouseMoveEvent(const double xPos, const double yPos) : x(xPos), y(yPos) {}
        static EventType StaticType() { return EventType::MouseMoveE; }
        [[nodiscard]] EventType GetEventType() const override { return StaticType(); }
        [[nodiscard]] double GetX() const { return x; }
        [[nodiscard]] double GetY() const { return y; }
    };

    class ZUES_API MouseButtonPressEvent final : public Event {
        int button;
    public:
        explicit MouseButtonPressEvent(const int btn) : button(btn) {}
        static EventType StaticType() { return EventType::MouseButtonPressE; }
        [[nodiscard]] EventType GetEventType() const override { return StaticType(); }
        [[nodiscard]] int GetButton() const { return button; }
    };

    class ZUES_API MouseButtonReleaseEvent final : public Event {
        int button;
    public:
        explicit MouseButtonReleaseEvent(const int btn) : button(btn) {}
        static EventType StaticType() { return EventType::MouseButtonReleaseE; }
        [[nodiscard]] EventType GetEventType() const override { return StaticType(); }
        [[nodiscard]] int GetButton() const { return button; }
    };

} // namespace Engine
