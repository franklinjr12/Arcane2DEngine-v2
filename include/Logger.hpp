#pragma once

#include "Arcane2DLib.hpp"

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

// for some reason the debug log is not working
#ifdef _DEBUG
#define A2D_LOGD(message, ...) spdlog::debug(message, __VA_ARGS__)
#define A2D_LOGI(message, ...) spdlog::info(message, __VA_ARGS__)
#define A2D_LOGW(message, ...) spdlog::warn(message, __VA_ARGS__)
#define A2D_LOGE(message, ...) spdlog::error(message, __VA_ARGS__)
#else
#define A2D_LOGD(message, ...) void(0)
#define A2D_LOGI(message, ...) void(0)
#define A2D_LOGW(message, ...) void(0)
#define A2D_LOGE(message, ...) spdlog::error(message, __VA_ARGS__)
#endif

A2D_API void set_arcane_logger();