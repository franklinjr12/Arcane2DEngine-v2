#include "arcane/animation.hpp"

#include <limits>
#include <memory>
#include <vector>

#include "arcane/component_registry.hpp"
#include "test_assert.hpp"

namespace {

arcane::AnimationClip make_three_frame_clip(bool loop) {
  arcane::AnimationClip clip;
  clip.name = "walk";
  clip.loop = loop;
  clip.frames.push_back({.uv = {0.0f, 0.0f, 0.25f, 1.0f}, .duration_seconds = 0.1f});
  clip.frames.push_back({.uv = {0.25f, 0.0f, 0.5f, 1.0f}, .duration_seconds = 0.2f});
  clip.frames.push_back({.uv = {0.5f, 0.0f, 0.75f, 1.0f}, .duration_seconds = 0.1f});
  return clip;
}

void test_clip_duration_sums_frame_durations() {
  const auto clip = make_three_frame_clip(true);
  ARCANE_ASSERT_TRUE(arcane::clip_duration(clip) == 0.4f);

  arcane::AnimationClip empty;
  ARCANE_ASSERT_TRUE(arcane::clip_duration(empty) == 0.0f);
}

void test_select_frame_index_picks_frame_at_elapsed_time() {
  const auto clip = make_three_frame_clip(true);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 0.0f) == 0);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 0.05f) == 0);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 0.15f) == 1);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 0.35f) == 2);

  arcane::AnimationClip empty;
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(empty, 1.0f) == 0);
}

void test_select_frame_index_loops_with_wraparound() {
  const auto clip = make_three_frame_clip(true);
  // Total duration is 0.4s; 0.45s should wrap to the same frame as 0.05s.
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 0.45f) == arcane::select_frame_index(clip, 0.05f));
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 4.35f) == 2);
}

void test_select_frame_index_clamps_to_last_frame_when_not_looping() {
  const auto clip = make_three_frame_clip(false);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 0.4f) == 2);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 100.0f) == 2);
}

void test_clip_json_round_trip() {
  const auto clip = make_three_frame_clip(false);
  const std::string json = arcane::save_clip_to_json(clip);

  auto loaded = arcane::load_clip_from_json(json);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->name == "walk");
  ARCANE_ASSERT_TRUE(loaded->loop == false);
  ARCANE_ASSERT_TRUE(loaded->frames.size() == 3);
  ARCANE_ASSERT_TRUE(loaded->frames[1].uv.u0 == 0.25f);
  ARCANE_ASSERT_TRUE(loaded->frames[1].duration_seconds == 0.2f);
}

void test_clip_json_rejects_wrong_format_version() {
  auto loaded = arcane::load_clip_from_json(R"({"format_version": 99, "name": "x", "loop": true, "frames": []})");
  ARCANE_ASSERT_TRUE(!loaded.has_value());
}

void test_animated_sprite_on_update_advances_elapsed_time_by_speed() {
  arcane::AnimatedSprite sprite;
  sprite.speed = 2.0f;
  sprite.on_update(0.1f);
  ARCANE_ASSERT_TRUE(sprite.elapsed_seconds() == 0.2f);

  sprite.playing = false;
  sprite.on_update(0.1f);
  ARCANE_ASSERT_TRUE(sprite.elapsed_seconds() == 0.2f);

  sprite.reset();
  ARCANE_ASSERT_TRUE(sprite.elapsed_seconds() == 0.0f);
}

void test_register_animation_components_registers_animated_sprite() {
  arcane::ComponentRegistry registry;
  arcane::register_animation_components(registry);
  ARCANE_ASSERT_TRUE(registry.contains("AnimatedSprite"));

  auto component = registry.create("AnimatedSprite");
  ARCANE_ASSERT_TRUE(component != nullptr);
  ARCANE_ASSERT_TRUE(registry.properties_of("AnimatedSprite")->size() == 3);
}


// Four 1-second frames (exact in float) so boundaries are exact.
arcane::AnimationClip make_unit_clip(bool loop, bool ping_pong = false) {
  arcane::AnimationClip clip;
  clip.loop = loop;
  clip.ping_pong = ping_pong;
  for (int i = 0; i < 4; ++i) clip.frames.push_back({.uv = {}, .duration_seconds = 1.0f});
  return clip;
}

using Events = std::vector<std::size_t>;

void test_ping_pong_selects_forward_then_backward() {
  const auto clip = make_unit_clip(true, true);
  const std::size_t expect[] = {0, 1, 2, 3, 3, 2, 1, 0, 0, 1};
  for (int i = 0; i < 10; ++i) {
    ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, static_cast<float>(i) + 0.5f) == expect[i]);
  }
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, -0.5f) == 0);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, -1.5f) == 1);
}

void test_once_clamps_and_finishes() {
  const auto clip = make_unit_clip(false);
  ARCANE_ASSERT_TRUE(!arcane::clip_finished(clip, 3.5f));
  ARCANE_ASSERT_TRUE(arcane::clip_finished(clip, 4.0f));
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, 1000.0f) == 3);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, -5.0f) == 0);
  ARCANE_ASSERT_TRUE(!arcane::clip_finished(make_unit_clip(true), 1000.0f));
}

void test_speed_zero_frozen_and_negative_reverses() {
  arcane::AnimatedSprite sprite;
  sprite.speed = 0.0f;
  for (int i = 0; i < 10; ++i) sprite.on_update(0.25f);
  ARCANE_ASSERT_TRUE(sprite.elapsed_seconds() == 0.0f);

  const auto clip = make_unit_clip(true);
  sprite.speed = -1.0f;
  sprite.on_update(0.5f);
  ARCANE_ASSERT_TRUE(sprite.previous_elapsed_seconds() == 0.0f);
  ARCANE_ASSERT_TRUE(sprite.elapsed_seconds() == -0.5f);
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, sprite.elapsed_seconds()) == 3);
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.0f, -0.5f) == Events{3}));

  sprite.speed = std::numeric_limits<float>::infinity();
  sprite.on_update(0.5f);
  ARCANE_ASSERT_TRUE(sprite.elapsed_seconds() == -0.5f);
}

void test_events_fire_once_per_crossing_when_skipping_frames() {
  const auto clip = make_unit_clip(true);
  ARCANE_ASSERT_TRUE(arcane::frame_events(clip, 0.0f, 0.9f).empty());
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.9f, 1.0f) == Events{1}));
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.5f, 3.5f) == Events{1, 2, 3}));
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 3.5f, 4.5f) == Events{0}));
  // Consecutive ticks never repeat a boundary, even landing exactly on it.
  Events all;
  float prev = 0.0f;
  for (int i = 1; i <= 16; ++i) {
    const float now = static_cast<float>(i) * 0.5f;
    for (auto e : arcane::frame_events(clip, prev, now)) all.push_back(e);
    prev = now;
  }
  ARCANE_ASSERT_TRUE((all == Events{1, 2, 3, 0, 1, 2, 3, 0}));
  // One tick spanning the whole clip twice and a bit.
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.0f, 9.0f) == Events{1, 2, 3, 0, 1, 2, 3, 0, 1}));
}

void test_events_reverse_mirror_forward() {
  const auto clip = make_unit_clip(true);
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 2.5f, 0.5f) == Events{1, 0}));
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.5f, -0.5f) == Events{3}));
  // Forward then back over the same boundary reports it each way.
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.9f, 1.1f) == Events{1}));
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 1.1f, 0.9f) == Events{0}));
}

void test_events_once_fire_end_frame_a_single_time() {
  const auto clip = make_unit_clip(false);
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.0f, 100.0f) == Events{1, 2, 3}));
  ARCANE_ASSERT_TRUE(arcane::frame_events(clip, 100.0f, 200.0f).empty());
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 3.5f, 0.0f) == Events{2, 1, 0}));
}

void test_events_ping_pong_do_not_repeat_turnaround_frames() {
  const auto clip = make_unit_clip(true, true);
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.0f, 8.0f) == Events{1, 2, 3, 2, 1, 0}));
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 0.0f, 12.0f) == Events{1, 2, 3, 2, 1, 0, 1, 2, 3}));
  ARCANE_ASSERT_TRUE((arcane::frame_events(clip, 6.5f, 0.5f) == Events{2, 3, 2, 1, 0}));
}

void test_events_huge_dt_and_degenerate_input() {
  const auto clip = make_unit_clip(true);
  ARCANE_ASSERT_TRUE(arcane::frame_events(clip, 0.0f, 1.0e6f).size() <= 64 * 4);
  ARCANE_ASSERT_TRUE(arcane::frame_events(clip, 0.0f, std::numeric_limits<float>::quiet_NaN()).empty());
  ARCANE_ASSERT_TRUE(arcane::frame_events(arcane::AnimationClip{}, 0.0f, 5.0f).empty());
  ARCANE_ASSERT_TRUE(arcane::select_frame_index(clip, std::numeric_limits<float>::infinity()) == 0);
}

}  // namespace

int main() {
  test_clip_duration_sums_frame_durations();
  test_select_frame_index_picks_frame_at_elapsed_time();
  test_select_frame_index_loops_with_wraparound();
  test_select_frame_index_clamps_to_last_frame_when_not_looping();
  test_clip_json_round_trip();
  test_clip_json_rejects_wrong_format_version();
  test_animated_sprite_on_update_advances_elapsed_time_by_speed();
  test_register_animation_components_registers_animated_sprite();
  test_ping_pong_selects_forward_then_backward();
  test_once_clamps_and_finishes();
  test_speed_zero_frozen_and_negative_reverses();
  test_events_fire_once_per_crossing_when_skipping_frames();
  test_events_reverse_mirror_forward();
  test_events_once_fire_end_frame_a_single_time();
  test_events_ping_pong_do_not_repeat_turnaround_frames();
  test_events_huge_dt_and_degenerate_input();
  return 0;
}
