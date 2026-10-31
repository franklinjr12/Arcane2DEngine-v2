#pragma once

#include <cstddef>
#include <expected>
#include <map>
#include <string>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/property.hpp"

namespace arcane {

class ComponentRegistry;

// Animation state machine asset (`arcane.animgraph` v2, docs/formats/animgraph.md): states play clips, transitions
// fire on parameter conditions. Parameters are bool, float or trigger (a trigger is true until a transition consumes it).
enum class AnimParamType { Bool, Float, Trigger };
enum class AnimCompare { IsTrue, IsFalse, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, Fired };

struct AnimParam {
  std::string name;
  AnimParamType type = AnimParamType::Bool;
  float default_value = 0.0f;  // bool: 0 or 1; ignored for triggers
};

struct AnimCondition {
  std::string param;
  AnimCompare op = AnimCompare::IsTrue;
  float value = 0.0f;  // compared against by the float operators only
};

// `frame` is the clip frame index whose start fires the event.
struct AnimEvent {
  int frame = 0;
  std::string name;
};

struct AnimState {
  std::string name;
  AssetId clip = 0;
  float speed = 1.0f;
  std::vector<AnimEvent> events;
};

// `from` is a state name, or "*" for any state. Among the transitions whose conditions all hold, the highest
// priority wins; ties go to the earlier one in the file. A transition with no conditions always holds.
struct AnimTransition {
  std::string from;
  std::string to;
  int priority = 0;
  std::vector<AnimCondition> conditions;
};

struct ARCANE_API AnimGraph {
  std::vector<AnimParam> parameters;
  std::string initial;  // name of the first state
  std::vector<AnimState> states;
  std::vector<AnimTransition> transitions;

  const AnimParam* find_param(const std::string& name) const noexcept;
  const AnimState* find_state(const std::string& name) const noexcept;
};

// Checks names are unique and non-empty, `initial`, transition ends and condition parameters exist, and operators
// match parameter types. Returns the first problem, or an empty string.
ARCANE_API std::string validate_animgraph(const AnimGraph& graph);

ARCANE_API std::expected<std::string, std::string> save_animgraph_canonical_json(const AnimGraph& graph);
ARCANE_API std::expected<AnimGraph, std::string> load_animgraph_from_json(const std::string& text);

// Reads strictly and rewrites canonically; used by project migration (there are no legacy graphs).
ARCANE_API std::expected<std::string, std::string> migrate_animgraph_json(const std::string& text);

ARCANE_API const char* anim_compare_name(AnimCompare op) noexcept;

// Current parameter values of a running graph.
struct AnimParamValues {
  std::map<std::string, float> values;  // bool as 0/1, trigger as 0/1 (1 = pending)
};

// Index into graph.transitions of the transition to take from state `from`, or -1.
ARCANE_API int pick_transition(const AnimGraph& graph, const std::string& from, const AnimParamValues& params);

// Names of `events` whose frame starts in (from_seconds, to_seconds] of playback of `clip` (a looping clip fires an
// event again on every pass). Pass from_seconds = -1 to include frame 0 at the start of a state. Frame order, then
// event order.
ARCANE_API std::vector<std::string> collect_frame_events(const AnimationClip& clip, const std::vector<AnimEvent>& events,
                                                         float from_seconds, float to_seconds);

// Drives the sibling AnimatedSprite from an AnimGraph asset: enters `initial`, sets the AnimatedSprite clip and speed
// of the current state, takes transitions as parameters change and delivers frame events through
// Component::on_animation_event. Needs the project assets through the active GameContext (or set_graph for tests).
// Gameplay sets parameters from its own on_update; transitions are evaluated in the Animator's next on_update.
class ARCANE_API Animator : public Component {
 public:
  AssetId graph = 0;

  bool set_bool(const std::string& name, bool value);
  bool set_float(const std::string& name, float value);
  bool set_trigger(const std::string& name);
  // False if the parameter does not exist (or has another type).
  bool get_bool(const std::string& name) const;
  float get_float(const std::string& name) const;

  const std::string& state() const noexcept { return state_; }

  // Uses `graph_data` and `clips` (by asset id) instead of loading assets; for headless tests.
  void set_graph(AnimGraph graph_data, std::map<AssetId, AnimationClip> clips);

  void on_update(float fixed_dt) override;

 private:
  bool load();
  void enter(const std::string& name);
  bool set_value(const std::string& name, AnimParamType type, float value);

  bool loaded_ = false;
  bool failed_ = false;
  AnimGraph data_;
  std::map<AssetId, AnimationClip> clips_;
  AnimParamValues params_;
  std::string state_;
  float last_elapsed_ = -1.0f;
};

ARCANE_API void register_animgraph_components(ComponentRegistry& registry);

}  // namespace arcane
