#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/node.hpp"

namespace arcane {

class ComponentRegistry;
class Scene;

// Easing curves: map progress t in [0,1] to eased progress (0 -> 0, 1 -> 1).
// BackOut overshoots and BounceOut bounces, so results may leave [0,1].
enum class Easing {
  Linear,
  QuadIn, QuadOut, QuadInOut,
  CubicIn, CubicOut, CubicInOut,
  SineIn, SineOut, SineInOut,
  BackOut,
  BounceOut,
};

ARCANE_API float ease(Easing easing, float t) noexcept;
// Stable text names ("linear", "quad_in", ...) used by authored data.
ARCANE_API std::string_view to_string(Easing easing) noexcept;
ARCANE_API std::optional<Easing> parse_easing(std::string_view name) noexcept;

// What a tween animates from `from` to `to`. `loops` is the number of passes
// (0 = forever); with `yoyo` odd passes run backwards. Times are in seconds.
struct TweenSpec {
  float from = 0.0f;
  float to = 1.0f;
  float duration = 1.0f;
  float delay = 0.0f;
  Easing easing = Easing::Linear;
  int loops = 1;
  bool yoyo = false;
};

struct TweenValue {
  float value = 0.0f;
  bool started = false;   // false while still inside the delay
  bool finished = false;  // true once the last pass is done; value is final
};

// Pure function of the spec and the time since the tween was created. A
// non-positive duration finishes as soon as the delay has passed.
ARCANE_API TweenValue evaluate_tween(const TweenSpec& spec, double elapsed_seconds) noexcept;

// Node properties that can be tweened by name: "position.x", "position.y",
// "rotation", "scale.x", "scale.y" and, on a node with a Sprite,
// "tint.r", "tint.g", "tint.b", "tint.a".
ARCANE_API bool is_node_tween_target(std::string_view target) noexcept;
ARCANE_API std::optional<float> read_node_tween_target(const Node& node, std::string_view target);
// Returns false if the target name is unknown or the node lacks the component.
ARCANE_API bool write_node_tween_target(Node& node, std::string_view target, float value);

using TweenId = std::uint64_t;
using TimerId = std::uint64_t;

// Runs tweens and nothing else. It is advanced by the caller with the fixed
// timestep (Scene::update's dt), so tweens are deterministic and match
// physics timing (docs/decisions.md). A tween bound to a node is dropped
// silently (no completion callback) once that node is destroyed.
class ARCANE_API TweenSystem {
 public:
  TweenSystem();
  ~TweenSystem();
  TweenSystem(TweenSystem&&) noexcept;
  TweenSystem& operator=(TweenSystem&&) noexcept;
  TweenSystem(const TweenSystem&) = delete;
  TweenSystem& operator=(const TweenSystem&) = delete;

  // Free-standing tween: `apply` receives each new value (e.g. a HUD element
  // field or a game variable). The callbacks must not outlive what they touch.
  TweenId start(const TweenSpec& spec, std::function<void(float)> apply, std::function<void()> on_complete = {});

  // Tween of a named node property (see is_node_tween_target). Returns 0 and
  // starts nothing if the node or target does not exist. The scene must
  // outlive the system's updates.
  TweenId start_node(Scene& scene, NodeId node, std::string_view target, const TweenSpec& spec,
                     std::function<void()> on_complete = {});

  bool cancel(TweenId id);
  std::size_t cancel_node(NodeId node);
  bool active(TweenId id) const;
  std::size_t active_count() const;

  void update(float dt);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// One-shot and repeating timers on the same fixed clock.
class ARCANE_API Timers {
 public:
  Timers();
  ~Timers();
  Timers(Timers&&) noexcept;
  Timers& operator=(Timers&&) noexcept;
  Timers(const Timers&) = delete;
  Timers& operator=(const Timers&) = delete;

  TimerId after(float seconds, std::function<void()> callback);
  TimerId every(float seconds, std::function<void()> callback);
  // Like after/every but dropped silently once `node` no longer exists.
  TimerId after_for_node(Scene& scene, NodeId node, float seconds, std::function<void()> callback);
  TimerId every_for_node(Scene& scene, NodeId node, float seconds, std::function<void()> callback);

  bool cancel(TimerId id);
  std::size_t active_count() const;

  void update(float dt);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Authored tween: animates one named property of its own node. All fields are
// registered properties, so they save, load and edit like any other
// component. An unknown `target` or `easing` makes the component inert.
class ARCANE_API Tween : public Component {
 public:
  std::string target = "position.x";
  float from = 0.0f;
  float to = 1.0f;
  bool use_current_from = false;  // read `from` from the node when playback starts
  float duration = 1.0f;
  float delay = 0.0f;
  std::string easing = "linear";
  int loops = 1;  // 0 = forever
  bool yoyo = false;
  bool autoplay = true;

  void on_start() override;
  void on_update(float fixed_dt) override;

  void play();  // restart from the beginning
  void stop() noexcept { playing_ = false; }
  bool playing() const noexcept { return playing_; }
  bool finished() const noexcept { return finished_; }

 private:
  TweenSpec spec_{};
  double elapsed_ = 0.0;
  bool playing_ = false;
  bool finished_ = false;
};

ARCANE_API void register_tween_components(ComponentRegistry& registry);

}  // namespace arcane
