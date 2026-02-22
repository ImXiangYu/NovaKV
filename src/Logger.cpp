//
// Created by 26708 on 2026/2/8.
//

#include "Logger.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {
auto g_level = LogLevel::Info;
std::ofstream g_file;
std::mutex g_mu;

const char *ToString(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warning:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Fatal:
      return "FATAL";
    default:
      return "OFF";
  }
}

std::string NowString() {
  using clock = std::chrono::system_clock;
  auto now = clock::now();
  std::time_t t = clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}
}  // namespace

void Logger::SetLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_level = level;
}

void Logger::SetOutputFile(const std::string &path) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_file.is_open()) {
    g_file.close();
  }
  if (!path.empty()) {
    g_file.open(path, std::ios::app);
  }
}

void Logger::Log(LogLevel level, const std::string &msg, const char *file,
                 int line) {
  std::lock_guard<std::mutex> lock(g_mu);

  if (g_level == LogLevel::Off || level < g_level) return;

  std::ostringstream oss;
  oss << "[" << NowString() << "]"
      << "[" << ToString(level) << "]"
      << "[" << file << ":" << line << "] " << msg;

  if (g_file.is_open()) {
    g_file << oss.str() << "\n";
    g_file.flush();
  } else {
    std::cout << oss.str() << "\n";
  }
}
