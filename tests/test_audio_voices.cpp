#include "arcane/audio.hpp"

#include "test_assert.hpp"

namespace {

using arcane::AudioBus;
using arcane::AudioPlayResult;

void test_per_sound_cap() {
  auto sys = arcane::AudioSystem::create(true);
  ARCANE_ASSERT_TRUE(sys);
  double now = 0;
  sys->set_clock([&] { return now; });
  sys->set_sound_limits("sfx/hit.wav", {.max_voices = 6});
  for (int i = 0; i < 48; ++i) {
    const auto r = sys->play("sfx/hit.wav");
    ARCANE_ASSERT_TRUE(r == (i < 6 ? AudioPlayResult::Played : AudioPlayResult::PlayedStealing));
  }
  ARCANE_ASSERT_TRUE(sys->active_voices("sfx/hit.wav") == 6);
  ARCANE_ASSERT_TRUE(sys->active_voices(AudioBus::Sfx) == 6);
  ARCANE_ASSERT_TRUE(sys->commands().size() == 48);
}

void test_priority_steal() {
  auto sys = arcane::AudioSystem::create(true);
  ARCANE_ASSERT_TRUE(sys);
  sys->set_clock([] { return 0.0; });
  sys->set_bus_max_voices(AudioBus::Sfx, 4);
  sys->set_sound_limits("sfx/boss.wav", {.priority = 10});
  for (int i = 0; i < 4; ++i) ARCANE_ASSERT_TRUE(sys->play("sfx/low.wav") == AudioPlayResult::Played);
  ARCANE_ASSERT_TRUE(sys->play("sfx/boss.wav") == AudioPlayResult::PlayedStealing);
  ARCANE_ASSERT_TRUE(sys->active_voices("sfx/low.wav") == 3);
  ARCANE_ASSERT_TRUE(sys->active_voices("sfx/boss.wav") == 1);
  ARCANE_ASSERT_TRUE(sys->active_voices(AudioBus::Sfx) == 4);
  for (int i = 0; i < 3; ++i) ARCANE_ASSERT_TRUE(sys->play("sfx/boss.wav") == AudioPlayResult::PlayedStealing);
  ARCANE_ASSERT_TRUE(sys->active_voices("sfx/boss.wav") == 4);
  ARCANE_ASSERT_TRUE(sys->play("sfx/low.wav") == AudioPlayResult::Dropped);
  ARCANE_ASSERT_TRUE(sys->active_voices("sfx/boss.wav") == 4);
  ARCANE_ASSERT_TRUE(sys->active_voices("sfx/low.wav") == 0);
  ARCANE_ASSERT_TRUE(sys->active_voices(AudioBus::Sfx) == 4);
  ARCANE_ASSERT_TRUE(sys->commands().size() == 9);
  // Music bus is unaffected by the Sfx cap.
  for (int i = 0; i < 5; ++i) ARCANE_ASSERT_TRUE(sys->play("music/loop.ogg", 1.0f, true, AudioBus::Music) == AudioPlayResult::Played);
  ARCANE_ASSERT_TRUE(sys->active_voices(AudioBus::Music) == 5);
  ARCANE_ASSERT_TRUE(sys->active_voices(AudioBus::Sfx) == 4);
}

void test_throttle() {
  auto sys = arcane::AudioSystem::create(true);
  ARCANE_ASSERT_TRUE(sys);
  double now = 0;
  sys->set_clock([&] { return now; });
  sys->set_sound_limits("sfx/step.wav", {.min_retrigger_interval = 0.1});
  ARCANE_ASSERT_TRUE(sys->play("sfx/step.wav") == AudioPlayResult::Played);
  now = 0.05;
  ARCANE_ASSERT_TRUE(sys->play("sfx/step.wav") == AudioPlayResult::Throttled);
  now = 0.11;
  ARCANE_ASSERT_TRUE(sys->play("sfx/step.wav") == AudioPlayResult::Played);
  ARCANE_ASSERT_TRUE(sys->active_voices("sfx/step.wav") == 2);
  ARCANE_ASSERT_TRUE(sys->commands().size() == 3);
}

void test_unlimited_and_stop_all() {
  auto sys = arcane::AudioSystem::create(true);
  ARCANE_ASSERT_TRUE(sys);
  for (int i = 0; i < 10; ++i) ARCANE_ASSERT_TRUE(sys->play("sfx/a.wav") == AudioPlayResult::Played);
  ARCANE_ASSERT_TRUE(sys->active_voices() == 10);
  sys->stop_all();
  ARCANE_ASSERT_TRUE(sys->active_voices() == 0);
}

}  // namespace

int main() {
  test_per_sound_cap();
  test_priority_steal();
  test_throttle();
  test_unlimited_and_stop_all();
  return 0;
}
