#include "arcane/tween.hpp"

#include <cmath>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
constexpr float kDt = 1.0f / 60.0f;

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void test_easing_endpoints_and_names() {
  for (int i = 0; i <= static_cast<int>(Easing::BounceOut); ++i) {
    const auto e = static_cast<Easing>(i);
    ARCANE_ASSERT_TRUE(near(ease(e, 0.0f), 0.0f));
    ARCANE_ASSERT_TRUE(near(ease(e, 1.0f), 1.0f));
    ARCANE_ASSERT_TRUE(parse_easing(to_string(e)) == e);
  }
  ARCANE_ASSERT_TRUE(!parse_easing("nope"));
  ARCANE_ASSERT_TRUE(near(ease(Easing::Linear, 0.25f), 0.25f));
  ARCANE_ASSERT_TRUE(near(ease(Easing::QuadIn, 0.5f), 0.25f));
  ARCANE_ASSERT_TRUE(near(ease(Easing::QuadOut, 0.5f), 0.75f));
  ARCANE_ASSERT_TRUE(near(ease(Easing::QuadInOut, 0.5f), 0.5f));
  ARCANE_ASSERT_TRUE(near(ease(Easing::CubicIn, 0.5f), 0.125f));
  ARCANE_ASSERT_TRUE(near(ease(Easing::SineInOut, 0.5f), 0.5f));
  ARCANE_ASSERT_TRUE(ease(Easing::BackOut, 0.8f) > 1.0f);  // overshoots
  ARCANE_ASSERT_TRUE(near(ease(Easing::Linear, 2.0f), 1.0f));  // clamped
}

void test_evaluate_delay_loop_yoyo() {
  TweenSpec s{.from = 10.0f, .to = 20.0f, .duration = 1.0f, .delay = 0.5f};
  ARCANE_ASSERT_TRUE(!evaluate_tween(s, 0.25).started);
  ARCANE_ASSERT_TRUE(near(evaluate_tween(s, 1.0).value, 15.0f));
  const auto end = evaluate_tween(s, 1.5);
  ARCANE_ASSERT_TRUE(end.finished && end.value == 20.0f);

  s.delay = 0.0f;
  s.loops = 2;
  ARCANE_ASSERT_TRUE(!evaluate_tween(s, 1.9).finished);
  ARCANE_ASSERT_TRUE(near(evaluate_tween(s, 1.5).value, 15.0f));
  ARCANE_ASSERT_TRUE(evaluate_tween(s, 2.0).finished);

  s.yoyo = true;  // second pass runs back
  ARCANE_ASSERT_TRUE(near(evaluate_tween(s, 1.25).value, 17.5f));
  ARCANE_ASSERT_TRUE(evaluate_tween(s, 2.0).value == 10.0f);

  s.loops = 0;  // forever
  ARCANE_ASSERT_TRUE(!evaluate_tween(s, 1000.25).finished);
  ARCANE_ASSERT_TRUE(near(evaluate_tween(s, 1000.25).value, 12.5f));

  s.loops = 1;
  s.duration = 0.0f;
  ARCANE_ASSERT_TRUE(evaluate_tween(s, 0.0).finished);
}

void test_fixed_step_completion_timing() {
  TweenSystem tweens;
  float value = -1.0f;
  int completed = 0;
  tweens.start({.from = 0.0f, .to = 1.0f, .duration = 1.0f}, [&](float v) { value = v; }, [&] { ++completed; });
  for (int i = 0; i < 59; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(completed == 0 && value < 1.0f && tweens.active_count() == 1);
  tweens.update(kDt);  // 60th step lands exactly on the duration
  ARCANE_ASSERT_TRUE(completed == 1 && value == 1.0f && tweens.active_count() == 0);
  tweens.update(kDt);
  ARCANE_ASSERT_TRUE(completed == 1);
}

void test_cancel_and_callback_restart() {
  TweenSystem tweens;
  int applied = 0;
  const TweenId id = tweens.start({.duration = 1.0f}, [&](float) { ++applied; });
  tweens.update(kDt);
  ARCANE_ASSERT_TRUE(tweens.cancel(id) && !tweens.active(id) && !tweens.cancel(id));
  tweens.update(kDt);
  ARCANE_ASSERT_TRUE(applied == 1);

  int chained = 0;  // a completion callback may start another tween
  tweens.start({.duration = 0.0f}, {}, [&] {
    tweens.start({.duration = 0.0f}, {}, [&] { ++chained; });
  });
  tweens.update(kDt);
  ARCANE_ASSERT_TRUE(chained == 0 && tweens.active_count() == 1);
  tweens.update(kDt);
  ARCANE_ASSERT_TRUE(chained == 1);
}

void test_node_tween_cancelled_on_destroy() {
  ComponentRegistry registry;
  register_builtin_components(registry);
  Scene scene;
  Node* node = scene.create_node("a");
  scene.add_component(node, "Sprite", registry);
  const NodeId id = node->id();
  TweenSystem tweens;
  int completed = 0;
  ARCANE_ASSERT_TRUE(tweens.start_node(scene, id, "position.x", {.from = 0, .to = 10, .duration = 1.0f},
                                       [&] { ++completed; }) != 0);
  ARCANE_ASSERT_TRUE(tweens.start_node(scene, id, "tint.a", {.from = 1, .to = 0, .duration = 1.0f}) != 0);
  ARCANE_ASSERT_TRUE(tweens.start_node(scene, id, "bogus", {}) == 0);
  for (int i = 0; i < 30; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(near(node->local_transform().position.x, 5.0f, 0.05f));
  auto* sprite = static_cast<Sprite*>(node->find_component("Sprite"));
  ARCANE_ASSERT_TRUE(near(sprite->tint.a, 0.5f, 0.05f));

  scene.destroy_node(id);  // queued but not flushed yet: already treated as gone
  tweens.update(kDt);
  ARCANE_ASSERT_TRUE(tweens.active_count() == 0 && completed == 0);
  scene.flush_destroyed();
  tweens.update(kDt);

  Node* other = scene.create_node("b");
  ARCANE_ASSERT_TRUE(tweens.start_node(scene, other->id(), "scale.x", {.duration = 1.0f}) != 0);
  ARCANE_ASSERT_TRUE(tweens.cancel_node(other->id()) == 1 && tweens.active_count() == 0);
  ARCANE_ASSERT_TRUE(tweens.start_node(scene, id, "scale.x", {}) == 0);  // destroyed node
}

void test_timers() {
  Timers timers;
  int once = 0, repeat = 0;
  timers.after(0.5f, [&] { ++once; });
  const TimerId r = timers.every(0.25f, [&] { ++repeat; });
  for (int i = 0; i < 60; ++i) timers.update(kDt);
  ARCANE_ASSERT_TRUE(once == 1 && repeat == 4 && timers.active_count() == 1);
  timers.update(1.0f);  // big step fires once per elapsed interval
  ARCANE_ASSERT_TRUE(repeat == 8);
  ARCANE_ASSERT_TRUE(timers.cancel(r) && timers.active_count() == 0);

  ComponentRegistry registry;
  register_builtin_components(registry);
  Scene scene;
  Node* n = scene.create_node("n");
  int bound = 0;
  timers.every_for_node(scene, n->id(), 0.1f, [&] { ++bound; });
  timers.update(0.1f);
  ARCANE_ASSERT_TRUE(bound == 1);
  scene.destroy_node(n->id());
  scene.flush_destroyed();
  timers.update(0.1f);
  ARCANE_ASSERT_TRUE(bound == 1 && timers.active_count() == 0);
}

void test_tween_component_runs_and_round_trips() {
  ComponentRegistry registry;
  register_builtin_components(registry);
  register_tween_components(registry);
  Scene scene;
  Node* node = scene.create_node("mover");
  node->local_transform().position.x = 4.0f;
  auto* tween = static_cast<Tween*>(scene.add_component(node, "Tween", registry));
  tween->target = "position.x";
  tween->use_current_from = true;
  tween->to = 14.0f;
  tween->duration = 1.0f;
  tween->easing = "quad_out";
  tween->yoyo = true;
  tween->loops = 2;

  const auto saved = scene.save_canonical_json(registry);
  ARCANE_ASSERT_TRUE(saved.has_value());
  auto loaded = Scene::load_from_json(*saved, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->save_canonical_json(registry) == saved);
  auto* again = static_cast<Tween*>(loaded->roots()[0]->find_component("Tween"));
  ARCANE_ASSERT_TRUE(again && again->easing == "quad_out" && again->yoyo && again->loops == 2 &&
                     again->use_current_from);

  for (int i = 0; i < 30; ++i) scene.update(kDt);
  ARCANE_ASSERT_TRUE(node->local_transform().position.x > 4.0f && !tween->finished());
  for (int i = 0; i < 90; ++i) scene.update(kDt);
  ARCANE_ASSERT_TRUE(tween->finished() && node->local_transform().position.x == 4.0f);  // yoyo ends back at from

  tween->easing = "wobble";  // unknown names are inert, not crashes
  tween->play();
  ARCANE_ASSERT_TRUE(!tween->playing());
}

}  // namespace

int main() {
  test_easing_endpoints_and_names();
  test_evaluate_delay_loop_yoyo();
  test_fixed_step_completion_timing();
  test_cancel_and_callback_restart();
  test_node_tween_cancelled_on_destroy();
  test_timers();
  test_tween_component_runs_and_round_trips();
  return 0;
}
