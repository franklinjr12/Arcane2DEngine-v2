#include "arcane/animgraph.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

#include "arcane/canonical_json.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_context.hpp"
#include "arcane/node.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {
constexpr const char* kFormat = "arcane.animgraph";

struct NamedOp {
  AnimCompare op;
  const char* name;
};
constexpr NamedOp kOps[] = {{AnimCompare::IsTrue, "is_true"}, {AnimCompare::IsFalse, "is_false"},
                            {AnimCompare::Less, "lt"},        {AnimCompare::LessEqual, "le"},
                            {AnimCompare::Greater, "gt"},     {AnimCompare::GreaterEqual, "ge"},
                            {AnimCompare::Equal, "eq"},       {AnimCompare::NotEqual, "ne"},
                            {AnimCompare::Fired, "fired"}};

const char* param_type_name(AnimParamType type) {
  switch (type) {
    case AnimParamType::Bool: return "bool";
    case AnimParamType::Float: return "float";
    case AnimParamType::Trigger: return "trigger";
  }
  return "bool";
}

bool op_fits(AnimCompare op, AnimParamType type) {
  switch (type) {
    case AnimParamType::Bool: return op == AnimCompare::IsTrue || op == AnimCompare::IsFalse;
    case AnimParamType::Trigger: return op == AnimCompare::Fired;
    case AnimParamType::Float:
      return op != AnimCompare::IsTrue && op != AnimCompare::IsFalse && op != AnimCompare::Fired;
  }
  return false;
}

bool holds(const AnimCondition& c, float v) {
  switch (c.op) {
    case AnimCompare::IsTrue:
    case AnimCompare::Fired: return v != 0.0f;
    case AnimCompare::IsFalse: return v == 0.0f;
    case AnimCompare::Less: return v < c.value;
    case AnimCompare::LessEqual: return v <= c.value;
    case AnimCompare::Greater: return v > c.value;
    case AnimCompare::GreaterEqual: return v >= c.value;
    case AnimCompare::Equal: return v == c.value;
    case AnimCompare::NotEqual: return v != c.value;
  }
  return false;
}
}  // namespace

const char* anim_compare_name(AnimCompare op) noexcept {
  for (const NamedOp& n : kOps) {
    if (n.op == op) return n.name;
  }
  return "is_true";
}

const AnimParam* AnimGraph::find_param(const std::string& name) const noexcept {
  for (const AnimParam& p : parameters) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

const AnimState* AnimGraph::find_state(const std::string& name) const noexcept {
  for (const AnimState& s : states) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

std::string validate_animgraph(const AnimGraph& graph) {
  std::set<std::string> seen;
  for (const AnimParam& p : graph.parameters) {
    if (p.name.empty()) return "a parameter has an empty name";
    if (!seen.insert(p.name).second) return std::format("duplicate parameter '{}'", p.name);
  }
  seen.clear();
  for (const AnimState& s : graph.states) {
    if (s.name.empty() || s.name == "*") return std::format("invalid state name '{}'", s.name);
    if (!seen.insert(s.name).second) return std::format("duplicate state '{}'", s.name);
    if (!(s.speed >= 0.0f)) return std::format("state '{}' has a negative speed", s.name);
    for (const AnimEvent& e : s.events) {
      if (e.frame < 0) return std::format("state '{}' has an event on a negative frame", s.name);
      if (e.name.empty()) return std::format("state '{}' has an event without a name", s.name);
    }
  }
  if (!graph.states.empty() && !graph.find_state(graph.initial)) {
    return std::format("initial state '{}' does not exist", graph.initial);
  }
  for (std::size_t i = 0; i < graph.transitions.size(); ++i) {
    const AnimTransition& t = graph.transitions[i];
    if (t.from != "*" && !graph.find_state(t.from)) return std::format("transition {}: unknown state '{}'", i, t.from);
    if (!graph.find_state(t.to)) return std::format("transition {}: unknown state '{}'", i, t.to);
    for (const AnimCondition& c : t.conditions) {
      const AnimParam* p = graph.find_param(c.param);
      if (!p) return std::format("transition {}: unknown parameter '{}'", i, c.param);
      if (!op_fits(c.op, p->type)) {
        return std::format("transition {}: operator '{}' does not fit {} parameter '{}'", i, anim_compare_name(c.op),
                           param_type_name(p->type), p->name);
      }
    }
  }
  return {};
}

std::expected<std::string, std::string> save_animgraph_canonical_json(const AnimGraph& graph) {
  nlohmann::json j;
  j["format"] = kFormat;
  j["version"] = 2;
  j["initial"] = graph.initial;
  j["parameters"] = nlohmann::json::array();
  for (const AnimParam& p : graph.parameters) {
    nlohmann::json e = {{"name", p.name}, {"type", param_type_name(p.type)}};
    if (p.type != AnimParamType::Trigger) e["default"] = p.default_value;
    j["parameters"].push_back(std::move(e));
  }
  j["states"] = nlohmann::json::array();
  for (const AnimState& s : graph.states) {
    nlohmann::json e = {{"name", s.name}, {"clip", s.clip}, {"speed", s.speed}, {"events", nlohmann::json::array()}};
    for (const AnimEvent& ev : s.events) e["events"].push_back({{"frame", ev.frame}, {"name", ev.name}});
    j["states"].push_back(std::move(e));
  }
  j["transitions"] = nlohmann::json::array();
  for (const AnimTransition& t : graph.transitions) {
    nlohmann::json e = {{"from", t.from}, {"to", t.to}, {"priority", t.priority}, {"conditions", nlohmann::json::array()}};
    for (const AnimCondition& c : t.conditions) {
      nlohmann::json cond = {{"param", c.param}, {"op", anim_compare_name(c.op)}};
      const bool needs_value = c.op != AnimCompare::IsTrue && c.op != AnimCompare::IsFalse && c.op != AnimCompare::Fired;
      if (needs_value) cond["value"] = c.value;
      e["conditions"].push_back(std::move(cond));
    }
    j["transitions"].push_back(std::move(e));
  }
  return canonical::dump(j);
}

std::expected<AnimGraph, std::string> load_animgraph_from_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("animation graph: {}", parsed.error()));
  try {
    const nlohmann::json& j = *parsed;
    canonical::require_object(j, "animation graph");
    if (canonical::detect_dialect(j, kFormat, "animation graph") != canonical::Dialect::V2) {
      return std::unexpected(std::string("animation graph: only format arcane.animgraph version 2 exists"));
    }
    canonical::require_only_keys(j, {"format", "version", "initial", "parameters", "states", "transitions"}, "animation graph");
    AnimGraph g;
    if (j.contains("initial")) g.initial = canonical::as_string(j["initial"], "graph 'initial'");
    auto array_of = [&](const char* key) -> const nlohmann::json& {
      static const nlohmann::json empty = nlohmann::json::array();
      if (!j.contains(key)) return empty;
      if (!j[key].is_array()) canonical::fail(std::format("animation graph: '{}' must be an array", key));
      return j[key];
    };
    for (const auto& e : array_of("parameters")) {
      canonical::require_object(e, "graph parameter");
      canonical::require_only_keys(e, {"name", "type", "default"}, "graph parameter");
      AnimParam p;
      p.name = canonical::as_string(e.value("name", nlohmann::json("")), "parameter 'name'");
      const std::string type = canonical::as_string(e.value("type", nlohmann::json("bool")), "parameter 'type'");
      if (type == "bool") p.type = AnimParamType::Bool;
      else if (type == "float") p.type = AnimParamType::Float;
      else if (type == "trigger") p.type = AnimParamType::Trigger;
      else canonical::fail(std::format("animation graph: unknown parameter type '{}'", type));
      if (e.contains("default")) {
        p.default_value = e["default"].is_boolean() ? (e["default"].get<bool>() ? 1.0f : 0.0f)
                                                    : canonical::as_float(e["default"], "parameter 'default'");
      }
      g.parameters.push_back(std::move(p));
    }
    for (const auto& e : array_of("states")) {
      canonical::require_object(e, "graph state");
      canonical::require_only_keys(e, {"name", "clip", "speed", "events"}, "graph state");
      AnimState s;
      s.name = canonical::as_string(e.value("name", nlohmann::json("")), "state 'name'");
      if (e.contains("clip")) s.clip = canonical::as_uint(e["clip"], "state 'clip'");
      if (e.contains("speed")) s.speed = canonical::as_float(e["speed"], "state 'speed'");
      if (e.contains("events")) {
        if (!e["events"].is_array()) canonical::fail("animation graph: state 'events' must be an array");
        for (const auto& ev : e["events"]) {
          canonical::require_object(ev, "state event");
          canonical::require_only_keys(ev, {"frame", "name"}, "state event");
          s.events.push_back({canonical::as_int(ev.value("frame", nlohmann::json(0)), "event 'frame'"),
                              canonical::as_string(ev.value("name", nlohmann::json("")), "event 'name'")});
        }
      }
      g.states.push_back(std::move(s));
    }
    for (const auto& e : array_of("transitions")) {
      canonical::require_object(e, "graph transition");
      canonical::require_only_keys(e, {"from", "to", "priority", "conditions"}, "graph transition");
      AnimTransition t;
      t.from = canonical::as_string(e.value("from", nlohmann::json("*")), "transition 'from'");
      t.to = canonical::as_string(e.value("to", nlohmann::json("")), "transition 'to'");
      if (e.contains("priority")) t.priority = canonical::as_int(e["priority"], "transition 'priority'");
      if (e.contains("conditions")) {
        if (!e["conditions"].is_array()) canonical::fail("animation graph: transition 'conditions' must be an array");
        for (const auto& c : e["conditions"]) {
          canonical::require_object(c, "transition condition");
          canonical::require_only_keys(c, {"param", "op", "value"}, "transition condition");
          AnimCondition cond;
          cond.param = canonical::as_string(c.value("param", nlohmann::json("")), "condition 'param'");
          const std::string op = canonical::as_string(c.value("op", nlohmann::json("is_true")), "condition 'op'");
          const auto it = std::find_if(std::begin(kOps), std::end(kOps), [&](const NamedOp& n) { return op == n.name; });
          if (it == std::end(kOps)) canonical::fail(std::format("animation graph: unknown operator '{}'", op));
          cond.op = it->op;
          if (c.contains("value")) cond.value = canonical::as_float(c["value"], "condition 'value'");
          t.conditions.push_back(std::move(cond));
        }
      }
      g.transitions.push_back(std::move(t));
    }
    if (const std::string problem = validate_animgraph(g); !problem.empty()) {
      return std::unexpected(std::format("animation graph: {}", problem));
    }
    return g;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::format("animation graph: {}", ex.what()));
  }
}

std::expected<std::string, std::string> migrate_animgraph_json(const std::string& text) {
  auto graph = load_animgraph_from_json(text);
  if (!graph) return std::unexpected(graph.error());
  return save_animgraph_canonical_json(*graph);
}

int pick_transition(const AnimGraph& graph, const std::string& from, const AnimParamValues& params) {
  int best = -1;
  for (std::size_t i = 0; i < graph.transitions.size(); ++i) {
    const AnimTransition& t = graph.transitions[i];
    if (t.from != "*" && t.from != from) continue;
    if (t.from == "*" && t.to == from) continue;  // "any state" does not re-enter the current state
    bool ok = true;
    for (const AnimCondition& c : t.conditions) {
      const auto it = params.values.find(c.param);
      if (!holds(c, it == params.values.end() ? 0.0f : it->second)) {
        ok = false;
        break;
      }
    }
    if (ok && (best < 0 || t.priority > graph.transitions[static_cast<std::size_t>(best)].priority)) best = static_cast<int>(i);
  }
  return best;
}

std::vector<std::string> collect_frame_events(const AnimationClip& clip, const std::vector<AnimEvent>& events,
                                              float from_seconds, float to_seconds) {
  std::vector<std::string> fired;
  const float total = clip_duration(clip);
  if (clip.frames.empty() || to_seconds <= from_seconds) return fired;
  std::vector<float> starts(clip.frames.size());
  float t = 0.0f;
  for (std::size_t i = 0; i < clip.frames.size(); ++i) {
    starts[i] = t;
    t += clip.frames[i].duration_seconds;
  }
  std::vector<const AnimEvent*> ordered;
  for (const AnimEvent& e : events) {
    if (e.frame >= 0 && static_cast<std::size_t>(e.frame) < clip.frames.size()) ordered.push_back(&e);
  }
  std::stable_sort(ordered.begin(), ordered.end(), [](const AnimEvent* a, const AnimEvent* b) { return a->frame < b->frame; });
  constexpr int kMaxPasses = 8;
  if (!clip.loop || total <= 0.0f) {
    for (const AnimEvent* e : ordered) {
      const float start = starts[static_cast<std::size_t>(e->frame)];
      if (start > from_seconds && start <= to_seconds) fired.push_back(e->name);
    }
    return fired;
  }
  // Looping: pass k of frame f starts at starts[f] + k * total. Emit pass by pass so the order follows playback.
  const int last_pass = static_cast<int>(std::floor(to_seconds / total));
  const int first_pass = std::max(0, static_cast<int>(std::floor(from_seconds / total)));
  for (int pass = first_pass; pass <= std::min(last_pass, first_pass + kMaxPasses); ++pass) {
    for (const AnimEvent* e : ordered) {
      const float start = starts[static_cast<std::size_t>(e->frame)] + static_cast<float>(pass) * total;
      if (start > from_seconds && start <= to_seconds) fired.push_back(e->name);
    }
  }
  return fired;
}

bool Animator::set_value(const std::string& name, AnimParamType type, float value) {
  if (!load()) return false;
  const AnimParam* p = data_.find_param(name);
  if (!p || p->type != type) return false;
  params_.values[name] = value;
  return true;
}

bool Animator::set_bool(const std::string& name, bool value) { return set_value(name, AnimParamType::Bool, value ? 1.0f : 0.0f); }
bool Animator::set_float(const std::string& name, float value) { return set_value(name, AnimParamType::Float, value); }
bool Animator::set_trigger(const std::string& name) { return set_value(name, AnimParamType::Trigger, 1.0f); }

bool Animator::get_bool(const std::string& name) const {
  const auto it = params_.values.find(name);
  return it != params_.values.end() && it->second != 0.0f;
}

float Animator::get_float(const std::string& name) const {
  const auto it = params_.values.find(name);
  return it == params_.values.end() ? 0.0f : it->second;
}

void Animator::set_graph(AnimGraph graph_data, std::map<AssetId, AnimationClip> clips) {
  data_ = std::move(graph_data);
  clips_ = std::move(clips);
  loaded_ = true;
  failed_ = false;
  params_.values.clear();
  for (const AnimParam& p : data_.parameters) params_.values[p.name] = p.type == AnimParamType::Trigger ? 0.0f : p.default_value;
  state_.clear();
}

bool Animator::load() {
  if (loaded_) return true;
  if (failed_ || graph == 0) return false;
  GameContext* context = active_game_context();
  if (!context) return false;
  failed_ = true;  // stays failed unless everything below works
  const auto path = context->asset_path(graph);
  if (!path) return false;
  auto text = read_text_file(*path);
  if (!text) return false;
  auto parsed = load_animgraph_from_json(*text);
  if (!parsed) return false;
  std::map<AssetId, AnimationClip> clips;
  for (const AnimState& s : parsed->states) {
    if (s.clip == 0 || clips.contains(s.clip)) continue;
    const auto clip_path = context->asset_path(s.clip);
    if (!clip_path) continue;
    auto clip_text = read_text_file(*clip_path);
    if (!clip_text) continue;
    if (auto clip = load_clip_from_json(*clip_text)) clips.emplace(s.clip, std::move(*clip));
  }
  set_graph(std::move(*parsed), std::move(clips));
  return true;
}

void Animator::enter(const std::string& name) {
  const AnimState* s = data_.find_state(name);
  if (!s) return;
  state_ = name;
  last_elapsed_ = -1.0f;
  if (auto* sprite = static_cast<AnimatedSprite*>(node()->find_component("AnimatedSprite"))) {
    sprite->clip = s->clip;
    sprite->speed = s->speed;
    sprite->reset();
  }
}

void Animator::on_update(float) {
  if (!load() || data_.states.empty() || !node()) return;
  if (state_.empty()) enter(data_.initial);

  // Take at most one transition per tick; consume the triggers of its conditions.
  const int taken = pick_transition(data_, state_, params_);
  if (taken >= 0) {
    const AnimTransition& t = data_.transitions[static_cast<std::size_t>(taken)];
    for (const AnimCondition& c : t.conditions) {
      if (c.op == AnimCompare::Fired) params_.values[c.param] = 0.0f;
    }
    enter(t.to);
  }

  auto* sprite = static_cast<AnimatedSprite*>(node()->find_component("AnimatedSprite"));
  const AnimState* s = data_.find_state(state_);
  if (!sprite || !s || s->events.empty()) return;
  const auto clip = clips_.find(s->clip);
  if (clip == clips_.end()) return;
  const float now = sprite->elapsed_seconds();
  for (const std::string& event : collect_frame_events(clip->second, s->events, last_elapsed_, now)) {
    for (const auto& component : node()->components()) component->on_animation_event(event);
  }
  last_elapsed_ = now;
}

void register_animgraph_components(ComponentRegistry& registry) {
  registry.register_component("Animator", [] { return std::make_unique<Animator>(); },
                              {make_property<Animator>("graph", PropertyType::AssetId, &Animator::graph)});
}

}  // namespace arcane
