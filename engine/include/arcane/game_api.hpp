#pragma once

#include <cstdint>

#include "arcane/export.hpp"

// Narrow, versioned ABI boundary between the engine and a native game
// module (.so/.dll). Only plain-C-compatible types cross this boundary;
// internal gameplay code on either side can be ordinary C++. The engine
// checks engine_api_version before touching anything else in the struct.
namespace arcane {

inline constexpr std::uint32_t kGameApiVersion = 1;

struct GameApi {
  std::uint32_t api_version;
  const char* game_name;
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
