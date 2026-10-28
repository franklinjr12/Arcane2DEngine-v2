#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/property.hpp"

namespace arcane {

class ComponentRegistry;

// Which mixer bus a sound plays through; doc section 7.6's "simple
// master/music/SFX mixer-bus model" minus the master bus, which is just
// "both buses together" for now -- no separate master gain stage until a
// game actually needs one.
enum class AudioBus { Music = 0, Sfx = 1 };

// One play() call, recorded by AudioSystem regardless of backend (see
// AudioSystem::commands). Lets headless tests assert on *what* an
// AudioSource/one-shot call asked to play without needing a real audio
// device or decoding a real sound file.
struct AudioPlayCommand {
  std::filesystem::path file;
  float volume = 1.0f;
  bool loop = false;
  AudioBus bus = AudioBus::Sfx;
};

// Arcane-owned audio playback, backed by miniaudio's ma_engine. Every
// project/Play-mode runtime owns exactly one of these (see
// ProjectRuntime), analogous to PhysicsWorld.
class ARCANE_API AudioSystem {
 public:
  // `null_backend` selects miniaudio's null backend (see test_audio_null.cpp
  // for the equivalent raw-miniaudio smoke test): no real playback device is
  // opened, and play() below skips the real ma_engine call entirely, only
  // recording it -- tests must never construct a real-backend AudioSystem,
  // see docs/decisions.md.
  static std::expected<AudioSystem, std::string> create(bool null_backend);

  AudioSystem(AudioSystem&&) noexcept;
  AudioSystem& operator=(AudioSystem&&) noexcept;
  AudioSystem(const AudioSystem&) = delete;
  AudioSystem& operator=(const AudioSystem&) = delete;
  ~AudioSystem();

  bool is_null_backend() const noexcept;

  // Fire-and-forget playback: used both for AudioSource::play_on_start (see
  // arcane_editor/audio_wiring.hpp) and for gameplay-triggered one-shot SFX
  // via active_audio_system() below. There is no handle to later stop or
  // adjust this specific sound -- a looping AudioSource plays until the
  // engine/scene tears down; see docs/decisions.md.
  void play(const std::filesystem::path& file, float volume = 1.0f, bool loop = false, AudioBus bus = AudioBus::Sfx);

  void set_bus_volume(AudioBus bus, float volume) noexcept;
  float bus_volume(AudioBus bus) const noexcept;

  // Every play() call so far, oldest first. Real-backend AudioSystems also
  // keep appending here (not just the null backend) so the same assertions
  // work regardless of backend.
  const std::vector<AudioPlayCommand>& commands() const noexcept;
  void clear_commands() noexcept;

 private:
  AudioSystem();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// A sound bound to a node (doc section 7.6's AudioSource-like component).
// `clip` is resolved to a file path the same way Sprite::texture is
// resolved to a GPU texture -- by whichever cache the editor/player
// maintains (see arcane_editor/scene_render.hpp's AudioClipCache), not by
// this component itself. Plain data only: see arcane_editor/audio_wiring.hpp
// for the play_on_start trigger logic.
class ARCANE_API AudioSource : public Component {
 public:
  AssetId clip = 0;
  float volume = 1.0f;
  bool loop = false;
  bool play_on_start = false;
};

ARCANE_API void register_audio_components(ComponentRegistry& registry);

// The AudioSystem live for the current Play/runtime session, set by the
// player/editor main loop so gameplay code (Component::on_update,
// GameApi::on_update) can fire one-shot sounds without every call site
// threading a reference through. Mirrors active_input_map (see
// arcane/input.hpp). Null outside Play.
ARCANE_API void set_active_audio_system(AudioSystem* system) noexcept;
ARCANE_API AudioSystem* active_audio_system() noexcept;

}  // namespace arcane
