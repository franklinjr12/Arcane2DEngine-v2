#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <format>
#include <string>
#include <string_view>

#include "arcane/export.hpp"

namespace arcane {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

ARCANE_API std::string_view to_string(LogLevel level) noexcept;

// Level filtering. A message is emitted only if its level is >= the category's
// override (when set) or else the global minimum. Default minimum: Trace.
ARCANE_API void log_set_min_level(LogLevel level);
ARCANE_API LogLevel log_min_level();
ARCANE_API void log_set_category_level(std::string_view category, LogLevel level);
ARCANE_API void log_clear_category_level(std::string_view category);
ARCANE_API bool log_would_log(LogLevel level, std::string_view category);

// Pluggable sinks receive every message that passes the filter.
using LogSink = std::function<void(LogLevel, std::string_view category, std::string_view message)>;
ARCANE_API int log_add_sink(LogSink sink);  // returns an id for log_remove_sink
ARCANE_API void log_remove_sink(int id);
ARCANE_API void log_set_stderr_enabled(bool enabled);  // default true

struct LogFileOptions {
  std::size_t max_bytes = 0;  // 0 = unlimited; else rotate before exceeding
  int max_files = 1;          // rotated files kept: path.1 (newest) .. path.N
  bool timestamps = false;    // wall-clock "HH:MM:SS|" prefix; off keeps output deterministic
};

// File sink; each line is "LEVEL|category|message\n". Pass an empty path to
// close it. Appends to an existing file. Not thread-safe to reconfigure
// concurrently with log_message from other threads.
ARCANE_API void log_set_file(const std::filesystem::path& path, const LogFileOptions& options = {});
ARCANE_API void log_flush();  // the file sink is also flushed on close and at shutdown

ARCANE_API void log_message(LogLevel level, std::string_view category, std::string_view message);

template <class... Args>
void log(LogLevel level, std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
  log_message(level, category, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace arcane

#define ARCANE_LOG_TRACE(category, ...) ::arcane::log(::arcane::LogLevel::Trace, category, __VA_ARGS__)
#define ARCANE_LOG_DEBUG(category, ...) ::arcane::log(::arcane::LogLevel::Debug, category, __VA_ARGS__)
#define ARCANE_LOG_INFO(category, ...) ::arcane::log(::arcane::LogLevel::Info, category, __VA_ARGS__)
#define ARCANE_LOG_WARN(category, ...) ::arcane::log(::arcane::LogLevel::Warn, category, __VA_ARGS__)
#define ARCANE_LOG_ERROR(category, ...) ::arcane::log(::arcane::LogLevel::Error, category, __VA_ARGS__)
