//
// Created by 26708 on 2026/2/8.
//

#ifndef NOVAKV_LOGGER_H
#define NOVAKV_LOGGER_H

#include <string>

enum class LogLevel {
    Trace = 0, Debug = 1, Info = 2, Warning = 3,
    Error = 4, Fatal = 5, Off = 6
};

class Logger {
    public:
        static void SetLevel(LogLevel level);
        static void SetOutputFile(const std::string& path);
        static void Log(LogLevel level, const std::string& msg,
            const char* file, int line);

};

#define LOG_TRACE(msg) Logger::Log(LogLevel::Trace, (msg), __FILE__, __LINE__)
#define LOG_DEBUG(msg) Logger::Log(LogLevel::Debug, (msg), __FILE__, __LINE__)
#define LOG_INFO(msg)  Logger::Log(LogLevel::Info,  (msg), __FILE__, __LINE__)
#define LOG_WARN(msg)  Logger::Log(LogLevel::Warning, (msg), __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::Log(LogLevel::Error, (msg), __FILE__, __LINE__)
#define LOG_FATAL(msg) Logger::Log(LogLevel::Fatal, (msg), __FILE__, __LINE__)

#endif //NOVAKV_LOGGER_H