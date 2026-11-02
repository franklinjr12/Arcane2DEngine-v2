// Animation state machine (arcane/animgraph.hpp): file round trip and validation, transition choice, frame events and
// the Animator component driving a sibling AnimatedSprite.
#include "arcane/animgraph.hpp"

#include <memory>

#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

AnimationClip run_clip() {
  AnimationClip clip;
  clip.name = "run";
  for (int i = 0; i < 4; ++i) clip.frames.push_back({.uv = {}, .duration_seconds = 0.1f});  // 0.4 s loop
  return clip;
}

AnimGraph make_graph() {
  AnimGraph g;
  g.parameters = {{"moving", AnimParamType::Bool, 0.0f}, {"speed", AnimParamType::Float, 0.0f}, {"jump", AnimParamType::Trigger, 0.0f}};
  g.initial = "idle";
  g.states = {{"idle", 1, 1.0f, {}}, {"run", 2, 2.0f, {{0, "step"}, {2, "step"}}}, {"jump", 3, 1.0f, {}}};
  g.transitions = {
      {"idle", "run", 0, {{"moving", AnimCompare::IsTrue, 0.0f}}},
      {"run", "idle", 0, {{"moving", AnimCompare::IsFalse, 0.0f}}},
      {"*", "jump", 5, {{"jump", AnimCompare::Fired, 0.0f}}},
      {"jump", "idle", 0, {}},
  };
  return g;
}

void test_round_trip_and_validation() {
  const AnimGraph g = make_graph();
  ARCANE_ASSERT_TRUE(validate_animgraph(g).empty());
  const auto text = save_animgraph_canonical_json(g);
  ARCANE_ASSERT_TRUE(text.has_value());
  const auto loaded = load_animgraph_from_json(*text);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(save_animgraph_canonical_json(*loaded).value() == *text);
  ARCANE_ASSERT_TRUE(loaded->states[1].events.size() == 2 && loaded->transitions[2].from == "*");
  ARCANE_ASSERT_TRUE(migrate_animgraph_json(*text).value() == *text);

  AnimGraph bad = g;
  bad.transitions[0].conditions[0].op = AnimCompare::Fired;  // trigger operator on a bool
  ARCANE_ASSERT_TRUE(!validate_animgraph(bad).empty());
  bad = g;
  bad.transitions[0].to = "nowhere";
  ARCANE_ASSERT_TRUE(!validate_animgraph(bad).empty());
  bad = g;
  bad.states.push_back(bad.states[0]);
  ARCANE_ASSERT_TRUE(!validate_animgraph(bad).empty());
  ARCANE_ASSERT_TRUE(!load_animgraph_from_json(R"({"format":"arcane.animgraph","version":2,"extra":1})").has_value());
}

void test_pick_transition() {
  AnimGraph g = make_graph();
  AnimParamValues p;
  ARCANE_ASSERT_TRUE(pick_transition(g, "idle", p) == -1);
  p.values["moving"] = 1.0f;
  ARCANE_ASSERT_TRUE(pick_transition(g, "idle", p) == 0);
  p.values["jump"] = 1.0f;
  ARCANE_ASSERT_TRUE(pick_transition(g, "idle", p) == 2);  // priority 5 beats priority 0
  ARCANE_ASSERT_TRUE(pick_transition(g, "jump", p) == 3);  // "*" does not re-enter the current state
  // Float comparison and tie-break by file order.
  g.transitions = {{"idle", "run", 1, {{"speed", AnimCompare::Greater, 0.5f}}}, {"idle", "jump", 1, {}}};
  p.values.clear();
  p.values["speed"] = 0.4f;
  ARCANE_ASSERT_TRUE(pick_transition(g, "idle", p) == 1);
  p.values["speed"] = 0.6f;
  ARCANE_ASSERT_TRUE(pick_transition(g, "idle", p) == 0);
}

void test_frame_events() {
  const AnimationClip clip = run_clip();
  const std::vector<AnimEvent> events = {{0, "a"}, {2, "b"}};
  auto names = [&](float from, float to) { return collect_frame_events(clip, events, from, to); };
  ARCANE_ASSERT_TRUE(names(-1.0f, 0.0f) == std::vector<std::string>{"a"});  // frame 0 at the start
  ARCANE_ASSERT_TRUE(names(0.0f, 0.15f).empty());
  ARCANE_ASSERT_TRUE(names(0.15f, 0.25f) == std::vector<std::string>{"b"});  // frame 2 starts at 0.2
  ARCANE_ASSERT_TRUE(names(0.25f, 0.39f).empty());
  ARCANE_ASSERT_TRUE((names(0.39f, 0.45f) == std::vector<std::string>{"a"}));  // looped
  ARCANE_ASSERT_TRUE((names(-1.0f, 0.85f) == std::vector<std::string>{"a", "b", "a", "b", "a"}));
  AnimationClip once = clip;
  once.loop = false;
  ARCANE_ASSERT_TRUE(collect_frame_events(once, events, 0.39f, 0.85f).empty());
  ARCANE_ASSERT_TRUE(collect_frame_events(once, {{9, "out of range"}}, -1.0f, 5.0f).empty());
}

class Listener : public Component {
 public:
  std::vector<std::string> heard;
  void on_animation_event(const std::string& name) override { heard.push_back(name); }
};

void test_animator() {
  ComponentRegistry registry;
  register_animation_components(registry);
  register_animgraph_components(registry);
  registry.register_component("Listener", [] { return std::make_unique<Listener>(); }, {});
  ARCANE_ASSERT_TRUE(registry.properties_of("Animator") != nullptr);

  Scene scene;
  Node* node = scene.create_node("Hero");
  auto* sprite = static_cast<AnimatedSprite*>(scene.add_component(node, "AnimatedSprite", registry));
  auto* animator = static_cast<Animator*>(scene.add_component(node, "Animator", registry));
  auto* listener = static_cast<Listener*>(scene.add_component(node, "Listener", registry));
  animator->set_graph(make_graph(), {{2, run_clip()}});
  ARCANE_ASSERT_TRUE(animator->set_bool("moving", false) && !animator->set_bool("nope", true) && !animator->set_float("moving", 1.0f));

  const float dt = 1.0f / 60.0f;
  scene.update(dt);
  ARCANE_ASSERT_TRUE(animator->state() == "idle" && sprite->clip == 1);
  animator->set_bool("moving", true);
  scene.update(dt);
  ARCANE_ASSERT_TRUE(animator->state() == "run" && sprite->clip == 2 && sprite->speed == 2.0f);
  ARCANE_ASSERT_TRUE(listener->heard == std::vector<std::string>{"step"});  // frame 0 on entry
  for (int i = 0; i < 12; ++i) scene.update(dt);  // 13 ticks at speed 2 ~ 0.43 s of clip time: frame 2 and a loop
  ARCANE_ASSERT_TRUE(listener->heard.size() >= 3);
  animator->set_trigger("jump");
  scene.update(dt);
  ARCANE_ASSERT_TRUE(animator->state() == "jump" && !animator->get_bool("jump"));  // trigger consumed
  scene.update(dt);
  ARCANE_ASSERT_TRUE(animator->state() == "idle");
}

}  // namespace

int main() {
  test_round_trip_and_validation();
  test_pick_transition();
  test_frame_events();
  test_animator();
  std::puts("test_animgraph OK");
  return 0;
}
