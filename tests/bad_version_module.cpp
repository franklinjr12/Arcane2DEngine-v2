// Test fixture: a module that always rejects the engine's API version, to
// exercise GameModule::load's version-mismatch rejection path.
#include "arcane/game_api.hpp"

ARCANE_GAME_API const arcane::GameApi* arcane_get_game_api(std::uint32_t /*engine_api_version*/) {
  return nullptr;
}
