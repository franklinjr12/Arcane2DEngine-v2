#include "arcane/module.hpp"

#include <format>

#include "arcane/log.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace arcane {

namespace {
constexpr std::string_view kLogCategory = "module";

void* platform_load(const std::filesystem::path& path, std::string& error_out) {
#if defined(_WIN32)
  HMODULE handle = LoadLibraryW(path.c_str());
  if (!handle) {
    error_out = std::format("LoadLibrary failed for {} (error {})", path.string(), GetLastError());
    return nullptr;
  }
  return reinterpret_cast<void*>(handle);
#else
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    error_out = std::format("dlopen failed for {}: {}", path.string(), dlerror());
    return nullptr;
  }
  return handle;
#endif
}

void* platform_symbol(void* handle, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
  return dlsym(handle, name);
#endif
}

void platform_unload(void* handle) {
#if defined(_WIN32)
  if (handle) {
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
  }
#else
  if (handle) {
    dlclose(handle);
  }
#endif
}
}  // namespace

std::expected<GameModule, std::string> GameModule::load(const std::filesystem::path& path) {
  std::string error;
  void* handle = platform_load(path, error);
  if (!handle) {
    ARCANE_LOG_ERROR(kLogCategory, "{}", error);
    return std::unexpected(error);
  }

  void* symbol = platform_symbol(handle, ARCANE_GAME_ENTRY_POINT_NAME);
  if (!symbol) {
    platform_unload(handle);
    auto message = std::format("module {} does not export {}", path.string(), ARCANE_GAME_ENTRY_POINT_NAME);
    ARCANE_LOG_ERROR(kLogCategory, "{}", message);
    return std::unexpected(message);
  }

  auto entry_point = reinterpret_cast<arcane_get_game_api_fn>(symbol);
  const GameApi* api = entry_point(kGameApiVersion);
  if (!api) {
    platform_unload(handle);
    auto message =
        std::format("module {} rejected engine API version {}", path.string(), kGameApiVersion);
    ARCANE_LOG_ERROR(kLogCategory, "{}", message);
    return std::unexpected(message);
  }
  if (api->api_version != kGameApiVersion) {
    platform_unload(handle);
    auto message = std::format("module {} reported mismatched api_version {} (expected {})", path.string(),
                                api->api_version, kGameApiVersion);
    ARCANE_LOG_ERROR(kLogCategory, "{}", message);
    return std::unexpected(message);
  }

  GameModule module;
  module.handle_ = handle;
  module.api_ = api;
  ARCANE_LOG_INFO(kLogCategory, "loaded {} ({})", path.string(), api->game_name ? api->game_name : "?");
  return module;
}

GameModule::GameModule(GameModule&& other) noexcept : handle_(other.handle_), api_(other.api_) {
  other.handle_ = nullptr;
  other.api_ = nullptr;
}

GameModule& GameModule::operator=(GameModule&& other) noexcept {
  if (this != &other) {
    platform_unload(handle_);
    handle_ = other.handle_;
    api_ = other.api_;
    other.handle_ = nullptr;
    other.api_ = nullptr;
  }
  return *this;
}

GameModule::~GameModule() { platform_unload(handle_); }

}  // namespace arcane
