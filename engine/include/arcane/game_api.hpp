#pragma once

#include <cstdint>

#include "arcane/export.hpp"

// Narrow, versioned ABI boundary between the engine and a native game
// module (.so/.dll). Only plain-C-compatible types cross this boundary;
// internal gameplay code on either side can be ordinary C++. The engine
// checks engine_api_version before touching anything else in the struct.
// `register_types` is the one exception: it takes a reference to the
// engine's ComponentRegistry (see component_registry.hpp) rather than a
// plain-C type, because the module is dlopen'd into the same process and
// built against the same engine headers -- there is no cross-compiler ABI
// boundary to protect against, only the engine/module version skew that
// api_version already guards.
namespace arcane {

class ComponentRegistry;

inline constexpr std::uint32_t kGameApiVersion = 2;

struct GameApi {
  std::uint32_t api_version;
  const char* game_name;
  // Called once, right after a successful load, before on_load. May be
  // nullptr if the module registers no component types.
  void (*register_types)(ComponentRegistry& registry);
  void (*on_load)();
  void (*on_unload)();
  void (*on_update)(float delta_seconds);
  void (*on_render)();
};

}  // namespace arcane

// Every game module must export exactly this symbol. It must return a
// pointer to a statically-owned GameApi (no allocation/lifetime handoff),
// or nullptr if engine_api_version is not supported by this module.
#define ARCANE_GAME_ENTRY_POINT_NAME "arcane_get_game_api"

extern "C" {
using arcane_get_game_api_fn = const arcane::GameApi* (*)(std::uint32_t engine_api_version);
}
