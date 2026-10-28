#include "arcane/audio.hpp"

#define MA_NO_ENCODING
#include <miniaudio.h>

#include "arcane/component_registry.hpp"

namespace arcane {

namespace {
AudioSystem* g_active_audio_system = nullptr;
}  // namespace

struct AudioSystem::Impl {
  bool null_backend = true;
  ma_engine engine{};
  bool engine_initialized = false;
  std::vector<AudioPlayCommand> commands;
  float music_volume = 1.0f;
  float sfx_volume = 1.0f;

  ~Impl() {
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

AudioSystem::AudioSystem() = default;
AudioSystem::AudioSystem(AudioSystem&&) noexcept = default;
AudioSystem& AudioSystem::operator=(AudioSystem&&) noexcept = default;
AudioSystem::~AudioSystem() = default;

bool AudioSystem::is_null_backend() const noexcept { return impl_->null_backend; }

void AudioSystem::play(const std::filesystem::path& file, float volume, bool loop, AudioBus bus) {
  impl_->commands.push_back(AudioPlayCommand{.file = file, .volume = volume, .loop = loop, .bus = bus});

  if (impl_->null_backend) return;

  const float bus_volume = (bus == AudioBus::Music) ? impl_->music_volume : impl_->sfx_volume;
  ma_sound_group* group = nullptr;  // single implicit group; see docs/decisions.md.
  ma_engine_play_sound(&impl_->engine, file.string().c_str(), group);
  (void)bus_volume;
  (void)loop;
}

void AudioSystem::set_bus_volume(AudioBus bus, float volume) noexcept {
  if (bus == AudioBus::Music) {
    impl_->music_volume = volume;
  } else {
    impl_->sfx_volume = volume;
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

void set_active_audio_system(AudioSystem* system) noexcept { g_active_audio_system = system; }

AudioSystem* active_audio_system() noexcept { return g_active_audio_system; }

}  // namespace arcane
