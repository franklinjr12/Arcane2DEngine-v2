#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/property.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

class ComponentRegistry;

// One frame of a sprite-sheet animation: which sub-rectangle of the
// AnimatedSprite's sibling Sprite::texture to show, and how long (seconds)
// to hold it before advancing.
struct AnimationFrame {
  UvRect uv{};
  float duration_seconds = 0.1f;
};

// A named, ordered sequence of frames. Clips are authored as their own JSON
// asset files (see save_clip_to_json/load_clip_from_json below) rather than
// inline in a scene, so the same clip can be reused by multiple nodes/scenes
// -- the same "asset referenced by id" shape as Sprite::texture.
struct AnimationClip {
  std::string name;
  std::vector<AnimationFrame> frames;
  bool loop = true;
};

// Total playback length of one pass through `clip` (sum of frame durations).
// 0 for a clip with no frames.
ARCANE_API float clip_duration(const AnimationClip& clip) noexcept;

// Picks which frame of `clip` should be showing after `elapsed_seconds` of
// playback. Looping clips wrap with fmod; non-looping clips clamp to the
// last frame once elapsed_seconds reaches the clip's total duration. Returns
// 0 for a clip with no frames (callers must check frames.empty() themselves
// before indexing).
ARCANE_API std::size_t select_frame_index(const AnimationClip& clip, float elapsed_seconds) noexcept;

// Canonical clip v2 (`arcane.clip`, docs/formats/clip.md). The reader takes v2
// (strict) and legacy format_version 1 (lenient about unknown fields).
ARCANE_API std::expected<std::string, std::string> save_clip_canonical_json(const AnimationClip& clip);
// Convenience wrapper; returns an empty string on failure.
ARCANE_API std::string save_clip_to_json(const AnimationClip& clip);
ARCANE_API std::expected<AnimationClip, std::string> load_clip_from_json(const std::string& text);
// Legacy files are checked as strictly as v2 ones; used by migration.
ARCANE_API std::expected<AnimationClip, std::string> load_clip_from_json_strict(const std::string& text);
ARCANE_API std::expected<std::string, std::string> migrate_clip_json(const std::string& text);

// Drives playback of an AnimationClip (referenced by AssetId, resolved the
// same way Sprite::texture is -- by whichever TextureCache-equivalent cache
// the editor/player maintains) on a node that also has a Sprite. Only the
// elapsed-time bookkeeping lives here; picking the UV rect for the current
// frame needs the resolved AnimationClip data, so that happens at render
// time (see arcane_editor/scene_render.hpp's AnimationClipCache), not here.
class ARCANE_API AnimatedSprite : public Component {
 public:
  AssetId clip = 0;
  float speed = 1.0f;
  bool playing = true;

  void on_update(float fixed_dt) override;

  float elapsed_seconds() const noexcept { return elapsed_seconds_; }
  void reset() noexcept { elapsed_seconds_ = 0.0f; }

 private:
  float elapsed_seconds_ = 0.0f;
};

ARCANE_API void register_animation_components(ComponentRegistry& registry);

}  // namespace arcane
