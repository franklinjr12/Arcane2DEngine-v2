#pragma once

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/property.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

class ComponentRegistry;
class Scene;

// Built-in, engine-provided component types (as opposed to a game module's
// own types): every project needs something that says "draw a texture here"
// and "this is the camera", so these two live in the engine itself and are
// registered before any game module's register_types() runs (see
// register_builtin_components below). Plain data only -- the editor/player
// read these fields each frame to drive SpriteRenderer, they don't do any
// rendering themselves.
class ARCANE_API Sprite : public Component {
 public:
  AssetId texture = 0;
  float width = 64.0f;
  float height = 64.0f;
  Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

// Marks a node as a camera; its node's world position is the camera center.
// `primary` picks which Camera a viewport/player uses when a scene has more
// than one (first primary one found, in preorder, wins).
class ARCANE_API Camera : public Component {
 public:
  float zoom = 1.0f;
  bool primary = true;
};

// Runtime state is transient; authored fields are registered and serialized.
class ARCANE_API Camera2D : public Component {
 public:
  bool primary = true;
  float zoom = 1.0f;
  EntityId target = 0;
  float smoothing = 0.0f;  // exponential rate per second; zero follows immediately
  Vec2 deadzone{};         // half-size in world units
  bool bounds_enabled = false;
  Vec2 bounds_min{};
  Vec2 bounds_max{};
  bool pixel_snap = false;
  float shake_amplitude = 0.0f;
  float shake_duration = 0.0f;
  int shake_seed = 1;

  // Restart shake deterministically, including after authored settings change.
  void start_shake();
  void advance(const Scene& scene, float dt);
  Vec2 view_center(int width, int height) const;
  void on_start() override;

 private:
  Vec2 center_{};
  Vec2 shake_offset_{};
  float shake_remaining_ = 0.0f;
  float shake_total_ = 0.0f;
  std::uint32_t random_state_ = 1;
  bool initialized_ = false;
};

// Registers Sprite and Camera into `registry` under the type names "Sprite"
// and "Camera". Call this once per ComponentRegistry before loading any
// scene JSON or calling a game module's register_types, so both built-in
// and module component types are available for the same registry/lookup.
ARCANE_API void register_builtin_components(ComponentRegistry& registry);

}  // namespace arcane
