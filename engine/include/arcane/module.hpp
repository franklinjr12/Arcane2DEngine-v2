#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "arcane/export.hpp"
#include "arcane/game_api.hpp"

namespace arcane {

// Cross-platform dlopen/LoadLibrary wrapper around a native game module.
// Owns the OS library handle; unloads it on destruction.
class ARCANE_API GameModule {
 public:
  static std::expected<GameModule, std::string> load(const std::filesystem::path& path);

  GameModule(GameModule&& other) noexcept;
  GameModule& operator=(GameModule&& other) noexcept;
  GameModule(const GameModule&) = delete;
  GameModule& operator=(const GameModule&) = delete;
  ~GameModule();

  const GameApi* api() const noexcept { return api_; }

 private:
  GameModule() = default;

  void* handle_ = nullptr;
  const GameApi* api_ = nullptr;
};

}  // namespace arcane
