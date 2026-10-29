#pragma once

#include <memory>
#include <optional>
#include <string>

#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"

namespace arcane::editor {

// Tracks the currently selected node in the Hierarchy/Inspector panels.
// Holds only an id (not a Node*) so it stays valid across scene reloads;
// callers resolve it against whichever Scene is "live" right now.
class Selection {
 public:
  void select(NodeId id) noexcept { selected_ = id; }
  void clear() noexcept { selected_.reset(); }
  bool has_selection() const noexcept { return selected_.has_value(); }
  std::optional<NodeId> selected() const noexcept { return selected_; }

 private:
  std::optional<NodeId> selected_;
};

enum class EditorMode { Edit, Play };

// Edit/Play/Stop state machine (doc 16 default leaning: isolate Play in a
// temporary runtime scene; never let it mutate the authored scene). Play
// snapshots `authored` via its own JSON round-trip (Scene has no copy
// ctor by design, see scene.hpp) into `runtime_`; Stop just drops it.
class PlayStopController {
 public:
  EditorMode mode() const noexcept { return mode_; }

  // Builds a fresh runtime Scene from `authored` and switches to Play.
  // Returns false (and stays in Edit) if the snapshot round-trip fails.
  bool play(const Scene& authored, const ComponentRegistry& registry) {
    if (mode_ == EditorMode::Play) return true;
    auto json = authored.save_to_json(registry);
    auto loaded = Scene::load_from_json(json, registry);
    if (!loaded) return false;
    runtime_ = std::make_unique<Scene>(std::move(*loaded));
    physics_.rebuild(*runtime_);
    mode_ = EditorMode::Play;
    return true;
  }

  // Advances the runtime scene by one fixed tick (components, then
  // physics). No-op outside Play.
  void tick(float fixed_dt) {
    if (mode_ != EditorMode::Play || !runtime_) return;
    runtime_->update(fixed_dt);
    physics_.step(*runtime_, fixed_dt);
  }

  // Bodies in the Play-mode physics world (0 in Edit mode).
  int physics_body_count() const noexcept { return mode_ == EditorMode::Play ? physics_.body_count() : 0; }

  // Discards the runtime scene; the authored scene was never touched.
  void stop() noexcept {
    runtime_.reset();
    physics_ = PhysicsWorld();
    mode_ = EditorMode::Edit;
  }

  // The scene the Viewport should render/edit this frame: the authored
  // scene in Edit mode, the runtime snapshot in Play mode.
  Scene* active_scene(Scene& authored) const noexcept {
    return mode_ == EditorMode::Play ? runtime_.get() : &authored;
  }

 private:
  EditorMode mode_ = EditorMode::Edit;
  std::unique_ptr<Scene> runtime_;
  PhysicsWorld physics_;
};

}  // namespace arcane::editor
