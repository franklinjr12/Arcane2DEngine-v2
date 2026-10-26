#pragma once

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/property.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

class ComponentRegistry;

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

// Registers Sprite and Camera into `registry` under the type names "Sprite"
// and "Camera". Call this once per ComponentRegistry before loading any
// scene JSON or calling a game module's register_types, so both built-in
// and module component types are available for the same registry/lookup.
ARCANE_API void register_builtin_components(ComponentRegistry& registry);

}  // namespace arcane
