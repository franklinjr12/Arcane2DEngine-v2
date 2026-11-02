#include "arcane/log.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace arcane {

namespace {
std::mutex g_log_mutex;
std::ofstream g_log_file;
std::filesystem::path g_log_path;
LogFileOptions g_log_options;
std::size_t g_log_size = 0;
LogLevel g_min_level = LogLevel::Trace;
std::map<std::string, LogLevel, std::less<>> g_category_levels;
std::vector<std::pair<int, LogSink>> g_sinks;
int g_next_sink_id = 1;
bool g_stderr_enabled = true;

bool passes(LogLevel level, std::string_view category) {
  const auto it = g_category_levels.find(category);
  return level >= (it != g_category_levels.end() ? it->second : g_min_level);
}

std::filesystem::path rotated(const std::filesystem::path& base, int n) {
  return std::filesystem::path(base.string() + "." + std::to_string(n));
}

void open_file() {
  std::error_code ec;
  g_log_size = std::filesystem::exists(g_log_path, ec) ? static_cast<std::size_t>(std::filesystem::file_size(g_log_path, ec)) : 0;
  g_log_file.open(g_log_path, std::ios::app | std::ios::binary);
}

void rotate_file() {
  g_log_file.close();  // Windows can't rename an open file
  std::error_code ec;
  if (g_log_options.max_files > 0) {
    std::filesystem::remove(rotated(g_log_path, g_log_options.max_files), ec);
    for (int i = g_log_options.max_files - 1; i >= 1; --i) {
      std::filesystem::rename(rotated(g_log_path, i), rotated(g_log_path, i + 1), ec);
    }
    std::filesystem::rename(g_log_path, rotated(g_log_path, 1), ec);
  } else {
    std::filesystem::remove(g_log_path, ec);
  }
  open_file();
}

struct FileCloser {
  ~FileCloser() {
    if (g_log_file.is_open()) g_log_file.close();
  }
} g_file_closer;  // flush on shutdown
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

void log_set_min_level(LogLevel level) {
  std::lock_guard lock(g_log_mutex);
  g_min_level = level;
}

LogLevel log_min_level() {
  std::lock_guard lock(g_log_mutex);
  return g_min_level;
}

void log_set_category_level(std::string_view category, LogLevel level) {
  std::lock_guard lock(g_log_mutex);
  g_category_levels[std::string(category)] = level;
}

void log_clear_category_level(std::string_view category) {
  std::lock_guard lock(g_log_mutex);
  const auto it = g_category_levels.find(category);
  if (it != g_category_levels.end()) g_category_levels.erase(it);
}

bool log_would_log(LogLevel level, std::string_view category) {
  std::lock_guard lock(g_log_mutex);
  return passes(level, category);
}

int log_add_sink(LogSink sink) {
  std::lock_guard lock(g_log_mutex);
  const int id = g_next_sink_id++;
  g_sinks.emplace_back(id, std::move(sink));
  return id;
}

void log_remove_sink(int id) {
  std::lock_guard lock(g_log_mutex);
  std::erase_if(g_sinks, [id](const auto& s) { return s.first == id; });
}

void log_set_stderr_enabled(bool enabled) {
  std::lock_guard lock(g_log_mutex);
  g_stderr_enabled = enabled;
}

void log_set_file(const std::filesystem::path& path, const LogFileOptions& options) {
  std::lock_guard lock(g_log_mutex);
  if (g_log_file.is_open()) {
    g_log_file.close();
  }
  g_log_path = path;
  g_log_options = options;
  if (!path.empty()) {
    open_file();
  }
}

void log_flush() {
  std::lock_guard lock(g_log_mutex);
  if (g_log_file.is_open()) g_log_file.flush();
}

void log_message(LogLevel level, std::string_view category, std::string_view message) {
  std::lock_guard lock(g_log_mutex);
  if (!passes(level, category)) return;

  if (g_stderr_enabled) {
    const auto now = std::chrono::system_clock::now();
    std::cerr << std::format("[{:%H:%M:%S}] [{}] [{}] {}\n", std::chrono::floor<std::chrono::milliseconds>(now),
                             to_string(level), category, message);
  }
  if (g_log_file.is_open()) {
    std::string line;
    if (g_log_options.timestamps) {
      line = std::format("{:%H:%M:%S}|", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
    }
    line += std::format("{}|{}|{}\n", to_string(level), category, message);
    if (g_log_options.max_bytes > 0 && g_log_size > 0 && g_log_size + line.size() > g_log_options.max_bytes) {
      rotate_file();
    }
    g_log_file << line;
    g_log_file.flush();
    g_log_size += line.size();
  }
  for (const auto& [id, sink] : g_sinks) {
    sink(level, category, message);
  }
}

}  // namespace arcane
