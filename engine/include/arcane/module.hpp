#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "arcane/export.hpp"
#include "arcane/game_api.hpp"

namespace arcane {

class ComponentRegistry;

// Cross-platform dlopen/LoadLibrary wrapper around a native game module.
// Owns the OS library handle. Component registry entries and components
// created from this module's factories hold a lifetime token (see
// register_types), so unloading is refused while module code may still run:
// unload() returns an error, and the destructor / move assignment log an error
// and leave the library mapped instead of calling dlclose/FreeLibrary. There
// is no hot reload; destroy scenes and registries before the module.
class ARCANE_API GameModule {
 public:
  static std::expected<GameModule, std::string> load(const std::filesystem::path& path);

  GameModule(GameModule&& other) noexcept;
  GameModule& operator=(GameModule&& other) noexcept;
  GameModule(const GameModule&) = delete;
  GameModule& operator=(const GameModule&) = delete;
  ~GameModule();

  const GameApi* api() const noexcept { return api_; }

  // Calls api()->register_types (if set) with `registry` tagging every entry
  // it registers with this module's lifetime token.
  void register_types(ComponentRegistry& registry) const;
  // Registry entries and components created from them that still reference
  // this module.
  long live_references() const noexcept;
  // Unloads the library unless live_references() > 0, in which case it
  // returns an error and stays loaded. No-op on an empty module.
  std::expected<void, std::string> unload();

 private:
  GameModule() = default;
  void release() noexcept;

  void* handle_ = nullptr;
  const GameApi* api_ = nullptr;
  std::shared_ptr<const void> lifetime_;
};

}  // namespace arcane
