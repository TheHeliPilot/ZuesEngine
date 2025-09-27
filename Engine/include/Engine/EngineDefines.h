//
// Created by bucka on 9/27/2025.
//

#ifndef ENGINE_ENGINEDEFINES_H
#define ENGINE_ENGINEDEFINES_H
#include <string>

#include <chrono>
#include "Log.h"

#define TIMESTAMP std::chrono::time_point<std::chrono::system_clock>

//Logs
#define LOGLEVEL_INFO Engine::Log::EngineLog::LogLevel::Info
#define LOGLEVEL_WARN Engine::Log::EngineLog::LogLevel::Warning
#define LOGLEVEL_ERR Engine::Log::EngineLog::LogLevel::Err

//default is info
inline void ENGINE_LOG(const std::string& msg) {
    Engine::Log::EngineLog::LogMessage(msg, Engine::Log::EngineLog::LogLevel::Info);
}

inline void ENGINE_LOG(const std::string& msg, const Engine::Log::EngineLog::LogLevel level) {
    Engine::Log::EngineLog::LogMessage(msg, level);
}


#endif //ENGINE_ENGINEDEFINES_H