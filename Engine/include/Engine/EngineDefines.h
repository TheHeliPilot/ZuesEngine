//
// Created by bucka on 9/27/2025.
//

#ifndef ENGINE_ENGINEDEFINES_H
#define ENGINE_ENGINEDEFINES_H
#include <string>

#include <chrono>
#include <iostream>

#include "Log.h"

#define TIMESTAMP std::chrono::time_point<std::chrono::system_clock>

//Logs
#define LOGLEVEL_INFO Engine::Log::EngineLog::LogLevel::Info
#define LOGLEVEL_WARN Engine::Log::EngineLog::LogLevel::Warning
#define LOGLEVEL_ERR Engine::Log::EngineLog::LogLevel::Err

#define LOG_INFO(msg) engine_log(msg, LOGLEVEL_INFO, __FILE__, __LINE__)
#define LOG_WARN(msg) engine_log(msg, LOGLEVEL_WARN, __FILE__, __LINE__)
#define LOG_ERROR(msg) engine_log(msg, LOGLEVEL_ERR, __FILE__, __LINE__)

inline void engine_log(const std::string& msg, const Engine::Log::EngineLog::LogLevel level, const std::string &file, const int line) {
    std::cout << msg << std::endl;
    Engine::Log::EngineLog::LogMessage(msg, level, file, line);
}


#endif //ENGINE_ENGINEDEFINES_H