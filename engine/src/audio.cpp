#include "arcane/audio.hpp"
#include "arcane/game_context.hpp"

#define MA_NO_ENCODING
#include <miniaudio.h>

#include "arcane/component_registry.hpp"

#include <algorithm>

namespace arcane {

namespace {
AudioSystem* g_active_audio_system = nullptr;
}  // namespace

struct AudioSystem::Impl {
  bool null_backend = true;
  ma_engine engine{};
  bool engine_initialized = false;
  struct Sound {
    ma_sound sound{};
    AudioBus bus;
    float volume;
    bool initialized = false;
    ~Sound() { if (initialized) ma_sound_uninit(&sound); }
  };
  std::vector<std::unique_ptr<Sound>> sounds;
  std::vector<AudioPlayCommand> commands;
  float music_volume = 1.0f;
  float sfx_volume = 1.0f;

  ~Impl() {
    sounds.clear();
    if (engine_initialized) {
      ma_engine_uninit(&engine);
    }
  }
};

std::expected<AudioSystem, std::string> AudioSystem::create(bool null_backend) {
  AudioSystem system;
  system.impl_ = std::make_unique<Impl>();
  system.impl_->null_backend = null_backend;

  if (null_backend) {
    // No ma_engine is initialized for the null backend: play() below only
    // records the command, so there is no device callback to drive at all.
    return system;
  }

  ma_engine_config config = ma_engine_config_init();
  if (ma_engine_init(&config, &system.impl_->engine) != MA_SUCCESS) {
    return std::unexpected("failed to initialize miniaudio engine");
  }
  system.impl_->engine_initialized = true;
  return system;
}

std::expected<AudioSystem, std::string> AudioSystem::create_preferred(
    bool force_null, std::string& diagnostic, Factory factory) {
  diagnostic.clear();
  if (force_null) return factory(true);
  auto device = factory(false);
  if (device) return device;
  diagnostic = device.error();
  auto fallback = factory(true);
  if (!fallback) return std::unexpected(diagnostic + "; null backend: " + fallback.error());
  return fallback;
}

AudioSystem::AudioSystem() = default;
AudioSystem::AudioSystem(AudioSystem&&) noexcept = default;
AudioSystem& AudioSystem::operator=(AudioSystem&&) noexcept = default;
AudioSystem::~AudioSystem() = default;

bool AudioSystem::is_null_backend() const noexcept { return impl_->null_backend; }

void AudioSystem::play(const std::filesystem::path& file, float volume, bool loop, AudioBus bus) {
  impl_->commands.push_back(AudioPlayCommand{.file = file, .volume = volume, .loop = loop, .bus = bus});

  if (impl_->null_backend) return;

  std::erase_if(impl_->sounds, [](const auto& sound) { return ma_sound_at_end(&sound->sound); });
  auto sound = std::make_unique<Impl::Sound>();
  sound->bus = bus;
  sound->volume = volume;
#if defined(_WIN32)
  const ma_result result = ma_sound_init_from_file_w(&impl_->engine, file.c_str(), MA_SOUND_FLAG_STREAM,
                                                     nullptr, nullptr, &sound->sound);
#else
  const ma_result result = ma_sound_init_from_file(&impl_->engine, file.c_str(), MA_SOUND_FLAG_STREAM,
                                                   nullptr, nullptr, &sound->sound);
#endif
  if (result != MA_SUCCESS) {
    return;
  }
  sound->initialized = true;
  ma_sound_set_volume(&sound->sound, volume * bus_volume(bus));
  ma_sound_set_looping(&sound->sound, loop ? MA_TRUE : MA_FALSE);
  if (ma_sound_start(&sound->sound) == MA_SUCCESS) impl_->sounds.push_back(std::move(sound));
}

void AudioSystem::stop_all() noexcept { impl_->sounds.clear(); }

void AudioSystem::set_bus_volume(AudioBus bus, float volume) noexcept {
  if (bus == AudioBus::Music) {
    impl_->music_volume = volume;
  } else {
    impl_->sfx_volume = volume;
  }
  for (const auto& sound : impl_->sounds) {
    if (sound->bus == bus) ma_sound_set_volume(&sound->sound, sound->volume * volume);
  }
}

float AudioSystem::bus_volume(AudioBus bus) const noexcept {
  return (bus == AudioBus::Music) ? impl_->music_volume : impl_->sfx_volume;
}

const std::vector<AudioPlayCommand>& AudioSystem::commands() const noexcept { return impl_->commands; }

void AudioSystem::clear_commands() noexcept { impl_->commands.clear(); }

void register_audio_components(ComponentRegistry& registry) {
  registry.register_component(
      "AudioSource", [] { return std::make_unique<AudioSource>(); },
      {
          make_property<AudioSource>("clip", PropertyType::AssetId, &AudioSource::clip),
          make_property<AudioSource>("volume", PropertyType::Float, &AudioSource::volume),
          make_property<AudioSource>("loop", PropertyType::Bool, &AudioSource::loop),
          make_property<AudioSource>("play_on_start", PropertyType::Bool, &AudioSource::play_on_start),
      });
}

bool AudioSource::play() const {
  GameContext* context = active_game_context();
  return context && context->play_sound(clip, volume, loop, loop ? AudioBus::Music : AudioBus::Sfx);
}

bool play_one_shot(AssetId clip, float volume) {
  GameContext* context = active_game_context();
  return context && context->play_sound(clip, volume);
}

void set_active_audio_system(AudioSystem* system) noexcept { g_active_audio_system = system; }

AudioSystem* active_audio_system() noexcept { return g_active_audio_system; }

}  // namespace arcane
