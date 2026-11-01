#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "arcane/component_registry.hpp"
#include "arcane/export.hpp"
#include "arcane/hud.hpp"
#include "arcane/project.hpp"
#include "arcane/scene.hpp"

namespace arcane {

// Runtime scene switching for game code (docs/formats/project.md "transition"):
// loads scene files, optionally fades out and in around the switch through the
// HUD, and carries persistent root nodes (player, music, ...) into the next
// scene with their live state. Time is passed in, so it runs headless.
class ARCANE_API SceneManager {
 public:
  enum class Phase { Idle, FadingOut, FadingIn };

  SceneManager(std::filesystem::path project_dir, const ComponentRegistry& registry, TransitionConfig config = {});
  ~SceneManager();
  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  const TransitionConfig& config() const noexcept { return config_; }
  void set_config(const TransitionConfig& config) noexcept { config_ = config; }

  // Installs `scene` as the current one at once (startup).
  void set_scene(Scene scene);
  // Null before the first set_scene/switch.
  Scene* scene() noexcept { return scene_.get(); }

  // Persistent nodes must be root nodes of the current scene. They survive
  // switches (the manager updates the node's ID; look it up again with
  // find_persistent). Returns false for unknown or non-root nodes.
  bool set_persistent(NodeId id, bool persistent);
  bool is_persistent(NodeId id) const { return persistent_.contains(id); }
  // Persistent root named `name` in the current scene, or nullptr.
  Node* find_persistent(const std::string& name) const;

  // Switches immediately (no fade): loads project-relative `scene_path`, moves
  // persistent nodes over and destroys the old scene's other nodes (their
  // on_destroy runs). On failure the current scene stays.
  std::expected<void, std::string> switch_now(const std::string& scene_path);

  // Starts a transition: fade out, switch at the fade's end, fade in. With
  // `fade` false, or zero-length fades, that part is skipped. A new request
  // replaces one that has not switched yet; during a fade-in it is picked up
  // after that fade finishes.
  void request_switch(std::string scene_path, bool fade = true);

  // Advances the transition by `dt` real seconds and updates the fade overlay.
  void update(float dt);

  Phase phase() const noexcept { return phase_; }
  bool busy() const noexcept { return phase_ != Phase::Idle || !pending_.empty(); }
  float fade_alpha() const noexcept { return alpha_; }  // 0 = clear, 1 = fully covered
  // Why the last queued switch failed ("" if it did not).
  const std::string& last_error() const noexcept { return last_error_; }
  const std::string& current_scene_path() const noexcept { return current_path_; }

  // Called after every successful switch with the new current scene.
  void set_on_switched(std::function<void(Scene&)> callback) { on_switched_ = std::move(callback); }
  // The fade overlay is a full-screen progress bar with id "scene_fade" in
  // `hud` (default: active_hud() at update time), present only while alpha > 0.
  void set_hud(Hud* hud) noexcept { hud_ = hud; }

 private:
  void perform_queued_switch();
  void apply_fade_overlay();

  std::filesystem::path project_dir_;
  const ComponentRegistry* registry_;
  TransitionConfig config_;
  std::unique_ptr<Scene> scene_;
  std::unordered_set<NodeId> persistent_;
  std::string current_path_;
  std::string pending_;
  bool pending_fade_ = true;
  bool queued_fade_ = true;  // whether the in-progress transition uses fades
  Phase phase_ = Phase::Idle;
  float timer_ = 0.0f;
  float alpha_ = 0.0f;
  std::string last_error_;
  std::function<void(Scene&)> on_switched_;
  Hud* hud_ = nullptr;
};

// The manager of the running game, set by the player/editor main loop so
// components can call request_switch (mirrors active_hud). Null otherwise.
ARCANE_API void set_active_scene_manager(SceneManager* manager) noexcept;
ARCANE_API SceneManager* active_scene_manager() noexcept;

}  // namespace arcane
