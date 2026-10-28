#pragma once

#include <vector>

#include "arcane/audio.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/asset_catalog.hpp"

namespace arcane::editor {

// Fires system.play() once for every AudioSource in `scene` that has
// play_on_start set and whose clip resolves to an entry in `audio_assets`
// (see list_audio_assets). Called when Play begins / the player loads a
// scene. Returns how many sounds were started. Looping sources use the
// Music bus, one-shots the Sfx bus.
int start_play_on_start_sounds(Scene& scene, AudioSystem& system, const std::vector<AssetEntry>& audio_assets);

}  // namespace arcane::editor
