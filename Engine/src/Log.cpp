//
// Created by bucka on 9/26/2025.
//

#include "../include/Engine/Log.h"

#include "../include/Engine/Engine.h"

Engine::Log::EngineLog::LogLevel Engine::Log::EngineLog::currentLogLevel = LOGLEVEL_INFO;

void Engine::Log::EngineLog::LogMessage(const std::string &message, LogLevel level) {
    IEventSystem->Dispatch(LogEvent(
                level, message
            ));
}

void Engine::Log::EngineLog::ChangeLogLevel(const LogLevel newLevel) {
    currentLogLevel = newLevel;
}
