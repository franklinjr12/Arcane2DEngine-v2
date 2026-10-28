#include <filesystem>
#include <fstream>

#include "arcane/animation.hpp"
#include "arcane/audio.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/audio_wiring.hpp"
#include "arcane_editor/scene_render.hpp"
#include "test_assert.hpp"

namespace {

void test_clip_and_audio_listing_and_clip_cache() {
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_anim_audio_assets";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  arcane::AnimationClip clip;
  clip.frames.push_back({.uv = {0.0f, 0.0f, 0.5f, 1.0f}, .duration_seconds = 0.1f});
  {
    std::ofstream out(dir / "walk.json");
    out << arcane::save_clip_to_json(clip);
  }
  { std::ofstream(dir / "bad.json") << "{ not json"; }
  { std::ofstream(dir / "jump.wav") << "x"; }
  { std::ofstream(dir / "a.png") << "x"; }

  ARCANE_ASSERT_TRUE(arcane::editor::list_animation_clip_assets(dir).size() == 2);
  const auto audio = arcane::editor::list_audio_assets(dir);
  ARCANE_ASSERT_TRUE(audio.size() == 1);

  arcane::editor::AnimationClipCache cache;
  cache.set_assets(arcane::editor::list_animation_clip_assets(dir));
  const auto good_id = arcane::editor::asset_id_for_relative_path("walk.json");
  const auto bad_id = arcane::editor::asset_id_for_relative_path("bad.json");
  const arcane::AnimationClip* loaded = cache.get(good_id);
  ARCANE_ASSERT_TRUE(loaded != nullptr && loaded->frames.size() == 1);
  ARCANE_ASSERT_TRUE(cache.get(bad_id) == nullptr);
  ARCANE_ASSERT_TRUE(cache.get(0) == nullptr);
  ARCANE_ASSERT_TRUE(cache.get(12345) == nullptr);

  std::filesystem::remove_all(dir);
}

void test_preview_animations_advances_elapsed() {
  arcane::ComponentRegistry registry;
  arcane::register_animation_components(registry);
  arcane::Scene scene;
  arcane::Node* node = scene.create_node("n");
  auto* animated = static_cast<arcane::AnimatedSprite*>(scene.add_component(node, "AnimatedSprite", registry));
  arcane::editor::preview_animations(scene, 0.25f);
  ARCANE_ASSERT_TRUE(animated->elapsed_seconds() == 0.25f);
}

void test_play_on_start_sounds_fire_once_per_flagged_source() {
  arcane::ComponentRegistry registry;
  arcane::register_audio_components(registry);
  arcane::Scene scene;
  const arcane::AssetId jump = arcane::editor::asset_id_for_relative_path("jump.wav");
  const arcane::AssetId theme = arcane::editor::asset_id_for_relative_path("theme.ogg");

  arcane::Node* a = scene.create_node("a");
  auto* sa = static_cast<arcane::AudioSource*>(scene.add_component(a, "AudioSource", registry));
  sa->clip = jump;
  sa->play_on_start = true;
  arcane::Node* b = scene.create_node("b", a);  // nested
  auto* sb = static_cast<arcane::AudioSource*>(scene.add_component(b, "AudioSource", registry));
  sb->clip = theme;
  sb->loop = true;
  sb->play_on_start = true;
  arcane::Node* c = scene.create_node("c");
  auto* sc = static_cast<arcane::AudioSource*>(scene.add_component(c, "AudioSource", registry));
  sc->clip = jump;  // play_on_start false
  arcane::Node* d = scene.create_node("d");
  auto* sd = static_cast<arcane::AudioSource*>(scene.add_component(d, "AudioSource", registry));
  sd->clip = 999;  // unresolved
  sd->play_on_start = true;

  std::vector<arcane::editor::AssetEntry> assets = {
      {jump, "jump.wav", "/assets/jump.wav"},
      {theme, "theme.ogg", "/assets/theme.ogg"},
  };
  auto system = arcane::AudioSystem::create(true);
  ARCANE_ASSERT_TRUE(system.has_value());
  ARCANE_ASSERT_TRUE(arcane::editor::start_play_on_start_sounds(scene, *system, assets) == 2);
  ARCANE_ASSERT_TRUE(system->commands().size() == 2);
  ARCANE_ASSERT_TRUE(system->commands()[0].file == "/assets/jump.wav");
  ARCANE_ASSERT_TRUE(system->commands()[0].bus == arcane::AudioBus::Sfx);
  ARCANE_ASSERT_TRUE(system->commands()[1].loop && system->commands()[1].bus == arcane::AudioBus::Music);
}

}  // namespace

int main() {
  test_clip_and_audio_listing_and_clip_cache();
  test_preview_animations_advances_elapsed();
  test_play_on_start_sounds_fire_once_per_flagged_source();
  return 0;
}
