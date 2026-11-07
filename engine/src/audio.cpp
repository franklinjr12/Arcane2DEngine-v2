#include "arcane/audio.hpp"
#include "arcane/game_context.hpp"

#define MA_NO_ENCODING
#include <miniaudio.h>

#include "arcane/component_registry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>

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
  struct Voice {
    std::filesystem::path file;
    AudioBus bus;
    int priority;
    std::uint64_t seq;
    std::unique_ptr<Sound> sound;  // null on the null backend
  };
  std::vector<Voice> voices;
  std::map<std::filesystem::path, SoundVoiceLimits> limits;
  std::map<std::filesystem::path, double> last_start;
  int bus_caps[2]{0, 0};
  Clock clock;
  std::uint64_t next_seq = 0;
  std::vector<AudioPlayCommand> commands;
  float music_volume = 1.0f;
  float sfx_volume = 1.0f;

  ~Impl() {
    voices.clear();
    if (engine_initialized) {
      ma_engine_uninit(&engine);
    }
  }
};

std::expected<AudioSystem, std::string> AudioSystem::create(bool null_backend) {
  AudioSystem system;
  system.impl_ = std::make_unique<Impl>();
  system.impl_->null_backend = null_backend;
  system.impl_->clock = [start = std::chrono::steady_clock::now()] {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  };

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

AudioPlayResult AudioSystem::play(const std::filesystem::path& file, float volume, bool loop, AudioBus bus) {
  Impl& im = *impl_;
  im.commands.push_back(AudioPlayCommand{.file = file, .volume = volume, .loop = loop, .bus = bus});

  if (!im.null_backend) {
    std::erase_if(im.voices, [](const auto& v) { return v.sound && ma_sound_at_end(&v.sound->sound); });
  }

  const auto limit_it = im.limits.find(file);
  const SoundVoiceLimits limits = limit_it != im.limits.end() ? limit_it->second : SoundVoiceLimits{};
  const double now = im.clock();
  if (limits.min_retrigger_interval > 0.0) {
    const auto last = im.last_start.find(file);
    if (last != im.last_start.end() && now - last->second < limits.min_retrigger_interval) {
      return AudioPlayResult::Throttled;
    }
  }

  bool stolen = false;
  if (limits.max_voices > 0 && active_voices(file) >= limits.max_voices) {
    const auto oldest = std::find_if(im.voices.begin(), im.voices.end(),
                                     [&](const auto& v) { return v.file == file; });  // voices are in seq order
    im.voices.erase(oldest);
    stolen = true;
  }
  const int cap = im.bus_caps[static_cast<int>(bus)];
  if (cap > 0 && active_voices(bus) >= cap) {
    auto victim = im.voices.end();
    for (auto it = im.voices.begin(); it != im.voices.end(); ++it) {
      if (it->bus != bus) continue;
      if (victim == im.voices.end() || it->priority < victim->priority) victim = it;
    }
    if (victim->priority > limits.priority) return AudioPlayResult::Dropped;
    im.voices.erase(victim);
    stolen = true;
  }

  std::unique_ptr<Impl::Sound> sound;
  if (!im.null_backend) {
    sound = std::make_unique<Impl::Sound>();
    sound->bus = bus;
    sound->volume = volume;
#if defined(_WIN32)
    const ma_result result = ma_sound_init_from_file_w(&im.engine, file.c_str(), MA_SOUND_FLAG_STREAM,
                                                       nullptr, nullptr, &sound->sound);
#else
    const ma_result result = ma_sound_init_from_file(&im.engine, file.c_str(), MA_SOUND_FLAG_STREAM,
                                                     nullptr, nullptr, &sound->sound);
#endif
    if (result != MA_SUCCESS) return AudioPlayResult::Dropped;
    sound->initialized = true;
    ma_sound_set_volume(&sound->sound, volume * bus_volume(bus));
    ma_sound_set_looping(&sound->sound, loop ? MA_TRUE : MA_FALSE);
    if (ma_sound_start(&sound->sound) != MA_SUCCESS) return AudioPlayResult::Dropped;
  }
  im.voices.push_back(Impl::Voice{file, bus, limits.priority, im.next_seq++, std::move(sound)});
  im.last_start[file] = now;
  return stolen ? AudioPlayResult::PlayedStealing : AudioPlayResult::Played;
}

void AudioSystem::stop_all() noexcept { impl_->voices.clear(); }

void AudioSystem::set_sound_limits(const std::filesystem::path& sound, const SoundVoiceLimits& limits) {
  impl_->limits[sound] = limits;
}

SoundVoiceLimits AudioSystem::sound_limits(const std::filesystem::path& sound) const {
  const auto it = impl_->limits.find(sound);
  return it != impl_->limits.end() ? it->second : SoundVoiceLimits{};
}

void AudioSystem::set_bus_max_voices(AudioBus bus, int max_voices) noexcept {
  impl_->bus_caps[static_cast<int>(bus)] = std::max(0, max_voices);
}

int AudioSystem::bus_max_voices(AudioBus bus) const noexcept { return impl_->bus_caps[static_cast<int>(bus)]; }

void AudioSystem::set_clock(Clock clock) { impl_->clock = std::move(clock); }

int AudioSystem::active_voices() const noexcept { return static_cast<int>(impl_->voices.size()); }

int AudioSystem::active_voices(AudioBus bus) const noexcept {
  return static_cast<int>(std::count_if(impl_->voices.begin(), impl_->voices.end(),
                                        [bus](const auto& v) { return v.bus == bus; }));
}

int AudioSystem::active_voices(const std::filesystem::path& sound) const noexcept {
  return static_cast<int>(std::count_if(impl_->voices.begin(), impl_->voices.end(),
                                        [&](const auto& v) { return v.file == sound; }));
}

void AudioSystem::set_bus_volume(AudioBus bus, float volume) noexcept {
  if (bus == AudioBus::Music) {
    impl_->music_volume = volume;
  } else {
    impl_->sfx_volume = volume;
  }
  for (const auto& voice : impl_->voices) {
    if (voice.sound && voice.sound->bus == bus) ma_sound_set_volume(&voice.sound->sound, voice.sound->volume * volume);
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
