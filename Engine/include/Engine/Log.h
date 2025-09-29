//
// Created by bucka on 9/26/2025.
//

#ifndef ENGINE_LOG_H
#define ENGINE_LOG_H

#include <string>

namespace Engine::Log {
    class EngineLog {
    public:
        enum class LogLevel { Info, Warning, Err };

        static void LogMessage(const std::string &message, LogLevel level, std::string file, int line);

        static void ChangeLogLevel(const LogLevel newLevel);

    private:
        static LogLevel currentLogLevel;
    };
}

#endif //ENGINE_LOG_H