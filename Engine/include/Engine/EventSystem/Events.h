#pragma once

#include "EventSystem.h"
#include <string>
#include <utility>
#include <format>
#include "../EngineDefines.h"

namespace Engine {

    class LogEvent final : public Event {
        std::string message;
        Log::EngineLog::LogLevel logLevel;

    public:
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

} // namespace Engine
