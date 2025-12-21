//
// Created by bucka on 9/26/2025.
//

#include "../include/Engine/Log.h"

#include "../include/Engine/Engine.h"

Engine::Log::EngineLog::LogLevel Engine::Log::EngineLog::currentLogLevel = LOGLEVEL_INFO;

void Engine::Log::EngineLog::LogMessage(const std::string &message, const LogLevel level, std::string file, const int line) {
    // Only dispatch if IEventSystem is initialized
    if (IEventSystem) {
        IEventSystem->Dispatch(LogEvent(
                    level, message, file, line
                ));
    }
}

void Engine::Log::EngineLog::ChangeLogLevel(const LogLevel newLevel) {
    currentLogLevel = newLevel;
}
