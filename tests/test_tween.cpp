#include "arcane/tween.hpp"

#include <cmath>
#include <iterator>
#include <string_view>
#include <vector>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
constexpr float kDt = 1.0f / 60.0f;

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

struct EaseRow {
  const char* name;
  float golden[3];  // t = 0.25, 0.5, 0.75 (computed independently from the standard formulas)
};

constexpr EaseRow kEaseTable[] = {
    {"linear", {0.250000f, 0.500000f, 0.750000f}},
    {"quad_in", {0.062500f, 0.250000f, 0.562500f}},
    {"quad_out", {0.437500f, 0.750000f, 0.937500f}},
    {"quad_in_out", {0.125000f, 0.500000f, 0.875000f}},
    {"cubic_in", {0.015625f, 0.125000f, 0.421875f}},
    {"cubic_out", {0.578125f, 0.875000f, 0.984375f}},
    {"cubic_in_out", {0.062500f, 0.500000f, 0.937500f}},
    {"sine_in", {0.076120f, 0.292893f, 0.617317f}},
    {"sine_out", {0.382683f, 0.707107f, 0.923880f}},
    {"sine_in_out", {0.146447f, 0.500000f, 0.853553f}},
    {"back_in", {-0.064137f, -0.087698f, 0.182590f}},
    {"back_out", {0.817410f, 1.087697f, 1.064137f}},
    {"back_in_out", {-0.099682f, 0.500000f, 1.099682f}},
    {"elastic_in", {-0.005524f, -0.015625f, 0.088388f}},
    {"elastic_out", {0.911612f, 1.015625f, 1.005524f}},
    {"elastic_in_out", {0.011969f, 0.500000f, 0.988031f}},
    {"bounce_in", {0.027344f, 0.234375f, 0.527344f}},
    {"bounce_out", {0.472656f, 0.765625f, 0.972656f}},
    {"bounce_in_out", {0.117188f, 0.500000f, 0.882812f}},
};

bool is_monotonic_curve(std::string_view name) {
  return !(name.starts_with("back") || name.starts_with("elastic") || name.starts_with("bounce"));
}

void test_easing_endpoints_and_names() {
  constexpr int count = static_cast<int>(Easing::BounceInOut) + 1;
  ARCANE_ASSERT_TRUE(count == static_cast<int>(std::size(kEaseTable)));
  for (int i = 0; i < count; ++i) {
    const auto e = static_cast<Easing>(i);
    const EaseRow& row = kEaseTable[i];
    ARCANE_ASSERT_TRUE(to_string(e) == row.name && parse_easing(row.name) == e);
    ARCANE_ASSERT_TRUE(ease(e, 0.0f) == 0.0f && ease(e, 1.0f) == 1.0f);  // exact, not approximate
    ARCANE_ASSERT_TRUE(ease(e, -3.0f) == 0.0f && ease(e, 3.0f) == 1.0f);  // clamped
    const float ts[3] = {0.25f, 0.5f, 0.75f};
    for (int k = 0; k < 3; ++k) ARCANE_ASSERT_TRUE(near(ease(e, ts[k]), row.golden[k], 2e-5f));
    if (is_monotonic_curve(row.name)) {
      float prev = 0.0f;
      for (int k = 1; k <= 200; ++k) {
        const float v = ease(e, float(k) / 200.0f);
        ARCANE_ASSERT_TRUE(v >= prev);
        prev = v;
      }
    }
  }
  ARCANE_ASSERT_TRUE(!parse_easing("nope"));
  ARCANE_ASSERT_TRUE(ease(Easing::BackOut, 0.8f) > 1.0f);  // overshoots
  ARCANE_ASSERT_TRUE(ease(Easing::BackIn, 0.3f) < 0.0f);   // undershoots
  ARCANE_ASSERT_TRUE(ease(Easing::ElasticOut, 0.5f) > 1.0f);
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

TweenStep step(float from, float to, float duration, float* out, float delay = 0.0f) {
  return {{.from = from, .to = to, .duration = duration, .delay = delay}, [out](float v) { *out = v; }};
}

void test_sequence_order_and_timing() {
  TweenSystem tweens;
  float a = -1.0f, b = -1.0f;
  int completed = 0;
  int order_a_done = -1, order_b_start = -1, frame = 0;
  // a: 0..1 over 0.5 s (30 frames); b waits 0.25 s then 10..20 over 0.5 s: starts at frame 45, ends at frame 75.
  auto sa = step(0, 1, 0.5f, &a);
  auto sb = step(10, 20, 0.5f, &b, 0.25f);
  auto apply_b = sb.apply;
  sb.apply = [&, apply_b](float v) { if (order_b_start < 0) order_b_start = frame; apply_b(v); };
  tweens.start_sequence({sa, sb}, [&] { ++completed; });
  float a_at_30 = 0.0f;
  for (frame = 1; frame <= 90; ++frame) {
    tweens.update(kDt);
    if (a == 1.0f && order_a_done < 0) order_a_done = frame;
    if (frame == 30) a_at_30 = a;
    if (frame < 45) ARCANE_ASSERT_TRUE(b == -1.0f);  // b untouched until its delay has passed
    if (frame < 75) ARCANE_ASSERT_TRUE(completed == 0);
    if (frame == 45) ARCANE_ASSERT_TRUE(near(b, 10.0f, 1e-3f));
    if (frame == 60) ARCANE_ASSERT_TRUE(near(b, 15.0f, 0.05f));
    if (frame == 75) ARCANE_ASSERT_TRUE(b == 20.0f && completed == 1);
  }
  ARCANE_ASSERT_TRUE(order_a_done == 30 && a_at_30 == 1.0f && order_b_start == 45);
  ARCANE_ASSERT_TRUE(b == 20.0f && completed == 1 && tweens.active_count() == 0);

  // Invalid: empty, or an endless step that is not last.
  ARCANE_ASSERT_TRUE(tweens.start_sequence({}) == 0);
  auto endless = step(0, 1, 1.0f, &a);
  endless.spec.loops = 0;
  ARCANE_ASSERT_TRUE(tweens.start_sequence({endless, step(0, 1, 1.0f, &b)}) == 0);
  ARCANE_ASSERT_TRUE(tweens.start_sequence({step(0, 1, 1.0f, &b), endless}) != 0);
}

void test_parallel_group() {
  TweenSystem tweens;
  float a = -1.0f, b = -1.0f;
  int completed = 0;
  tweens.start_parallel({step(0, 1, 0.5f, &a), step(0, 1, 1.0f, &b, 0.6f)}, [&] { ++completed; });
  for (int i = 0; i < 30; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(a == 1.0f && b == -1.0f && completed == 0);  // a done, b still delayed
  for (int i = 0; i < 65; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(completed == 0 && b < 1.0f);
  tweens.update(kDt);  // 1.6 s total (96 frames)
  ARCANE_ASSERT_TRUE(completed == 1 && b == 1.0f && tweens.active_count() == 0);
}

void test_cancel_chain() {
  TweenSystem tweens;
  float a = -1.0f, b = -1.0f;
  int completed = 0;
  TweenId id = tweens.start_sequence({step(0, 1, 0.5f, &a), step(5, 6, 0.5f, &b)}, [&] { ++completed; });
  for (int i = 0; i < 15; ++i) tweens.update(kDt);  // mid first step
  ARCANE_ASSERT_TRUE(a > 0.0f && a < 1.0f && b == -1.0f);
  const float a_frozen = a;
  ARCANE_ASSERT_TRUE(tweens.cancel(id) && !tweens.active(id));
  for (int i = 0; i < 90; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(a == a_frozen && b == -1.0f && completed == 0 && tweens.active_count() == 0);  // rest never runs

  // Snap to end: every step lands on its final value, still no on_complete.
  a = b = -1.0f;
  id = tweens.start_sequence({step(0, 1, 0.5f, &a), step(5, 6, 0.5f, &b)}, [&] { ++completed; });
  for (int i = 0; i < 15; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(tweens.cancel(id, true));
  ARCANE_ASSERT_TRUE(a == 1.0f && b == 6.0f && completed == 0);
  ARCANE_ASSERT_TRUE(!tweens.cancel(id));

  // Cancelling from inside a step's apply stops the rest of that update.
  TweenSystem other;
  int calls = 0;
  TweenId self = 0;
  TweenStep killer{{.duration = 1.0f}, [&](float) { ++calls; other.cancel(self); }};
  self = other.start_parallel({killer, step(0, 1, 1.0f, &a)}, [&] { ++completed; });
  a = -5.0f;
  other.update(kDt);
  ARCANE_ASSERT_TRUE(calls == 1 && a == -5.0f && completed == 0 && other.active_count() == 0);
}

void test_cancel_node_chains() {
  ComponentRegistry registry;
  register_builtin_components(registry);
  Scene scene;
  Node* node = scene.create_node("n");
  Node* other = scene.create_node("m");
  TweenSystem tweens;
  int completed = 0;
  tweens.start_node_sequence(scene, node->id(),
                             {{"position.x", {.from = 0, .to = 10, .duration = 0.5f}},
                              {"position.y", {.from = 0, .to = 20, .duration = 0.5f}}},
                             [&] { ++completed; });
  tweens.start_node_parallel(scene, node->id(), {{"scale.x", {.from = 1, .to = 3, .duration = 1.0f}}});
  tweens.start_node(scene, node->id(), "rotation", {.from = 0, .to = 1, .duration = 1.0f});
  tweens.start_node(scene, other->id(), "position.x", {.from = 0, .to = 8, .duration = 1.0f});
  ARCANE_ASSERT_TRUE(tweens.start_node_sequence(scene, node->id(), {{"bogus", {}}}) == 0);
  ARCANE_ASSERT_TRUE(tweens.start_node_sequence(scene, 9999, {{"position.x", {}}}) == 0);
  ARCANE_ASSERT_TRUE(tweens.active_count() == 4);
  for (int i = 0; i < 15; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(node->local_transform().position.x > 0.0f && node->local_transform().position.y == 0.0f);

  ARCANE_ASSERT_TRUE(tweens.cancel_node(node->id(), true) == 3);  // chain, group and single; not `other`
  const Transform2D& t = node->local_transform();
  ARCANE_ASSERT_TRUE(t.position.x == 10.0f && t.position.y == 20.0f && t.scale.x == 3.0f && t.rotation_radians == 1.0f);
  ARCANE_ASSERT_TRUE(tweens.active_count() == 1 && completed == 0);
  for (int i = 0; i < 60; ++i) tweens.update(kDt);
  ARCANE_ASSERT_TRUE(other->local_transform().position.x == 8.0f && completed == 0);

  // Destroying the node drops a chain silently.
  const NodeId gone = node->id();
  tweens.start_node_sequence(scene, gone, {{"position.x", {.from = 0, .to = 1, .duration = 1.0f}}}, [&] { ++completed; });
  scene.destroy_node(gone);
  scene.flush_destroyed();
  tweens.update(kDt);
  ARCANE_ASSERT_TRUE(tweens.active_count() == 0 && completed == 0);
}

}  // namespace

int main() {
  test_easing_endpoints_and_names();
  test_evaluate_delay_loop_yoyo();
  test_fixed_step_completion_timing();
  test_cancel_and_callback_restart();
  test_node_tween_cancelled_on_destroy();
  test_sequence_order_and_timing();
  test_parallel_group();
  test_cancel_chain();
  test_cancel_node_chains();
  test_timers();
  test_tween_component_runs_and_round_trips();
  return 0;
}
