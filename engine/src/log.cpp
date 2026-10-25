#include "arcane/log.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>

namespace arcane {

namespace {
std::mutex g_log_mutex;
std::ofstream g_log_file;
}  // namespace

std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

void log_set_file(const std::filesystem::path& path) {
  std::lock_guard lock(g_log_mutex);
  if (g_log_file.is_open()) {
    g_log_file.close();
  }
  if (!path.empty()) {
    g_log_file.open(path, std::ios::app);
  }
}

void log_message(LogLevel level, std::string_view category, std::string_view message) {
  const auto now = std::chrono::system_clock::now();
  const auto line = std::format("[{:%H:%M:%S}] [{}] [{}] {}\n", std::chrono::floor<std::chrono::milliseconds>(now),
                                 to_string(level), category, message);

  std::lock_guard lock(g_log_mutex);
  std::cerr << line;
  if (g_log_file.is_open()) {
    g_log_file << line;
    g_log_file.flush();
  }
}

}  // namespace arcane
