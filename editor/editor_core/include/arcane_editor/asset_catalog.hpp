#pragma once

#include "arcane/assets.hpp"

// The asset catalog lives in the engine library (arcane/assets.hpp) so gameplay code can resolve asset ids;
// these aliases keep the editor-side names.
namespace arcane::editor {

using arcane::asset_id_for_relative_path;
using arcane::AssetEntry;
using arcane::list_animation_clip_assets;
using arcane::list_audio_assets;
using arcane::list_image_assets;

}  // namespace arcane::editor
