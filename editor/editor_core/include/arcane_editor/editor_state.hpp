#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/project.hpp"
#include "arcane/scene.hpp"
#include "arcane/scene_flow.hpp"

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
// ctor by design, see scene.hpp) into a SceneManager that owns the runtime
// scene, so gameplay can call active_scene_manager()->request_switch(...);
// Stop just drops it.
class PlayStopController {
 public:
  PlayStopController() = default;
  ~PlayStopController() { stop(); }
  // The manager's on_switched callback points back at this object.
  PlayStopController(const PlayStopController&) = delete;
  PlayStopController& operator=(const PlayStopController&) = delete;

  EditorMode mode() const noexcept { return mode_; }

  // Builds a fresh runtime Scene from `authored` and switches to Play. The
  // scene runs under a SceneManager (project_dir resolves switch targets,
  // `transition` configures fades, `main_scene_path` is recorded as the
  // current scene path). Returns false (and stays in Edit) if the snapshot
  // round-trip fails.
  bool play(const Scene& authored, const ComponentRegistry& registry, std::filesystem::path project_dir = {},
            TransitionConfig transition = {}, std::string main_scene_path = {}) {
    if (mode_ == EditorMode::Play) return true;
    auto json = authored.save_to_json(registry);
    auto loaded = Scene::load_from_json(json, registry);
    if (!loaded) return false;
    manager_ = std::make_unique<SceneManager>(std::move(project_dir), registry, transition);
    manager_->set_scene(std::move(*loaded), std::move(main_scene_path));
    manager_->set_on_switched([this](Scene& scene) { physics_.rebuild(scene); });
    physics_.rebuild(*manager_->scene());
    set_active_scene_manager(manager_.get());
    mode_ = EditorMode::Play;
    return true;
  }

  // Advances the runtime scene by one fixed tick (components, then
  // physics, then the scene manager's transition). No-op outside Play.
  void tick(float fixed_dt) {
    if (mode_ != EditorMode::Play || !manager_) return;
    Scene* scene = manager_->scene();
    scene->update(fixed_dt);
    physics_.step(*scene, fixed_dt);
    manager_->update(fixed_dt);  // may switch scenes: re-fetch via active_scene()
  }

  // Bodies in the Play-mode physics world (0 in Edit mode).
  int physics_body_count() const noexcept { return mode_ == EditorMode::Play ? physics_.body_count() : 0; }

  // Discards the runtime scene; the authored scene was never touched.
  void stop() noexcept {
    if (active_scene_manager() == manager_.get() && manager_) set_active_scene_manager(nullptr);
    manager_.reset();
    physics_ = PhysicsWorld();
    mode_ = EditorMode::Edit;
  }

  // The scene the Viewport should render/edit this frame: the authored
  // scene in Edit mode, the runtime scene in Play mode (it changes when the
  // manager switches scenes).
  Scene* active_scene(Scene& authored) const noexcept {
    return mode_ == EditorMode::Play ? manager_->scene() : &authored;
  }

  // The Play-mode scene manager; null in Edit mode.
  SceneManager* scene_manager() noexcept { return manager_.get(); }

 private:
  EditorMode mode_ = EditorMode::Edit;
  std::unique_ptr<SceneManager> manager_;
  PhysicsWorld physics_;
};

}  // namespace arcane::editor
