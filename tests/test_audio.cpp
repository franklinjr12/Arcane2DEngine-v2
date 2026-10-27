#include "arcane/audio.hpp"

#include "arcane/component_registry.hpp"
#include "test_assert.hpp"

namespace {

void test_null_backend_records_commands_without_a_device() {
  auto system = arcane::AudioSystem::create(/*null_backend=*/true);
  ARCANE_ASSERT_TRUE(system.has_value());
  ARCANE_ASSERT_TRUE(system->is_null_backend());
  ARCANE_ASSERT_TRUE(system->commands().empty());

  system->play("music/theme.wav", 0.8f, true, arcane::AudioBus::Music);
  system->play("sfx/jump.wav");

  ARCANE_ASSERT_TRUE(system->commands().size() == 2);
  ARCANE_ASSERT_TRUE(system->commands()[0].file == "music/theme.wav");
  ARCANE_ASSERT_TRUE(system->commands()[0].volume == 0.8f);
  ARCANE_ASSERT_TRUE(system->commands()[0].loop == true);
  ARCANE_ASSERT_TRUE(system->commands()[0].bus == arcane::AudioBus::Music);
  ARCANE_ASSERT_TRUE(system->commands()[1].file == "sfx/jump.wav");
  ARCANE_ASSERT_TRUE(system->commands()[1].volume == 1.0f);
  ARCANE_ASSERT_TRUE(system->commands()[1].loop == false);
  ARCANE_ASSERT_TRUE(system->commands()[1].bus == arcane::AudioBus::Sfx);

  system->clear_commands();
  ARCANE_ASSERT_TRUE(system->commands().empty());
}

void test_bus_volume_get_set_defaults_to_full() {
  auto system = arcane::AudioSystem::create(/*null_backend=*/true);
  ARCANE_ASSERT_TRUE(system.has_value());
  ARCANE_ASSERT_TRUE(system->bus_volume(arcane::AudioBus::Music) == 1.0f);
  ARCANE_ASSERT_TRUE(system->bus_volume(arcane::AudioBus::Sfx) == 1.0f);

  system->set_bus_volume(arcane::AudioBus::Music, 0.3f);
  ARCANE_ASSERT_TRUE(system->bus_volume(arcane::AudioBus::Music) == 0.3f);
  ARCANE_ASSERT_TRUE(system->bus_volume(arcane::AudioBus::Sfx) == 1.0f);
}

void test_active_audio_system_defaults_to_null_outside_play() {
  ARCANE_ASSERT_TRUE(arcane::active_audio_system() == nullptr);

  auto system = arcane::AudioSystem::create(/*null_backend=*/true);
  ARCANE_ASSERT_TRUE(system.has_value());
  arcane::set_active_audio_system(&*system);
  ARCANE_ASSERT_TRUE(arcane::active_audio_system() == &*system);

  arcane::set_active_audio_system(nullptr);
  ARCANE_ASSERT_TRUE(arcane::active_audio_system() == nullptr);
}

void test_register_audio_components_registers_audio_source() {
  arcane::ComponentRegistry registry;
  arcane::register_audio_components(registry);
  ARCANE_ASSERT_TRUE(registry.contains("AudioSource"));

  auto component = registry.create("AudioSource");
  ARCANE_ASSERT_TRUE(component != nullptr);
  ARCANE_ASSERT_TRUE(registry.properties_of("AudioSource")->size() == 4);
}

}  // namespace

int main() {
  test_null_backend_records_commands_without_a_device();
  test_bus_volume_get_set_defaults_to_full();
  test_active_audio_system_defaults_to_null_outside_play();
  test_register_audio_components_registers_audio_source();
  return 0;
}
