#include "arcane/animation.hpp"

#include <memory>

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
  return 0;
}
