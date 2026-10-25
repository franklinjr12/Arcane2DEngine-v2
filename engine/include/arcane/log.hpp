#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include "arcane/export.hpp"

namespace arcane {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

ARCANE_API std::string_view to_string(LogLevel level) noexcept;

// Optional file sink in addition to the always-on stderr sink. Pass an empty
// path to disable the file sink. Not thread-safe to call concurrently with
// log_message from other threads.
ARCANE_API void log_set_file(const std::filesystem::path& path);

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
