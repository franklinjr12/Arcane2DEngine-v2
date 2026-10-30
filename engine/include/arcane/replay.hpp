#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "arcane/component_registry.hpp"
#include "arcane/export.hpp"
#include "arcane/input.hpp"
#include "arcane/math2d.hpp"
#include "arcane/scene.hpp"

namespace arcane {

// Deterministic input replay (docs/formats/replay.md). A recording is a text file (`arcane.replay` v1) holding
// the fixed rate, an optional seed and the input CHANGES per fixed frame. Replaying it steps a scene with the
// same fixed timestep and hashes the scene state after every frame. Determinism is only promised for the same
// build on the same platform (floating point and Box2D results may differ elsewhere).

enum class ReplayEventKind { Key, MouseButton, PadButton, PadAxis, MouseMove };

// One input change applied just before fixed frame `frame` runs; it stays in effect until changed again.
struct ReplayEvent {
  int frame = 0;
  ReplayEventKind kind = ReplayEventKind::Key;
  int pad = 0;       // PadButton, PadAxis
  int code = 0;      // key, mouse button, pad button or pad axis code
  bool down = false;  // Key, MouseButton, PadButton
  float value = 0.0f;  // PadAxis, in [-1, 1]
  Vec2 position{};   // MouseMove

  friend bool operator==(const ReplayEvent&, const ReplayEvent&) = default;
};

struct ReplayRecording {
  int fixed_hz = 60;  // 1..1000
  int frames = 1;     // number of fixed frames to run, 1..1000000
  // 0 = leave authored values alone. Otherwise every Int property named "seed" on every component is set to this
  // value before the first frame, so seeded systems (particles, camera shake) start identically.
  int seed = 0;
  std::vector<ReplayEvent> events;  // non-decreasing `frame`, each < frames

  friend bool operator==(const ReplayRecording&, const ReplayRecording&) = default;
};

ARCANE_API std::expected<std::string, std::string> save_recording_json(const ReplayRecording& recording);
// Strict: unknown fields, a missing/other format, an unsupported version and out-of-range values are errors.
ARCANE_API std::expected<ReplayRecording, std::string> load_recording_json(const std::string& text);

// Feeds a recording's input to the game one fixed frame at a time. advance() applies the events of the next
// frame to the internal backend and returns it; call it once per fixed step, then InputActionMap::update.
class ARCANE_API ReplayInput {
 public:
  explicit ReplayInput(ReplayRecording recording) : recording_(std::move(recording)) {}
  const ReplayRecording& recording() const noexcept { return recording_; }
  bool finished() const noexcept { return frame_ >= recording_.frames; }
  int frame() const noexcept { return frame_; }  // frames already advanced
  const InputBackend& advance();

 private:
  ReplayRecording recording_;
  FakeInputBackend backend_;
  int frame_ = 0;
  std::size_t next_event_ = 0;
};

// Records input changes: call capture() once per fixed frame (before stepping) with the live backend; it
// appends an event for each key/button/axis/mouse change since the previous call. `watch_*` choose what is polled.
class ARCANE_API ReplayRecorder {
 public:
  explicit ReplayRecorder(int fixed_hz = 60, int seed = 0);
  ~ReplayRecorder();
  ReplayRecorder(ReplayRecorder&&) noexcept;
  ReplayRecorder& operator=(ReplayRecorder&&) noexcept;

  // Choose what capture() polls: individual codes, or all six axes of a pad (0..kMaxGamepads-1).
  void watch_key(int code);
  void watch_mouse_button(int code);
  void watch_pad_button(int pad, int code);
  void watch_pad_axes(int pad);
  void capture(const InputBackend& backend);
  // Ends the recording after the frames captured so far (at least 1).
  ReplayRecording finish() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Canonical hash (FNV-1a 64) of the scene's gameplay state: for every node in tree order its id, name, local
// transform bits, pending-destroy flag and every component's id, type and registered property values; plus the
// live particle count. Float -0 hashes like +0.
// Sets every Int property named "seed" to recording.seed (no-op for seed 0); run_replay does this itself.
ARCANE_API void apply_replay_seed(Scene& scene, const ComponentRegistry& registry, int seed);

ARCANE_API std::uint64_t hash_scene_state(const Scene& scene, const ComponentRegistry& registry);

// Steps `scene` for recording.frames fixed frames driven by the recording's input, with physics, and returns the
// state hash after each frame. `actions` is the input map the scene's components read (it is made the active map
// for the duration). The scene is advanced in place; pass a fresh copy to keep the original.
ARCANE_API std::expected<std::vector<std::uint64_t>, std::string> run_replay(Scene& scene,
                                                                           const ComponentRegistry& registry,
                                                                           InputActionMap& actions,
                                                                           const ReplayRecording& recording);

// Display-free fixed-frame stepping. With `recording`, frames/fixed_hz/seed come from it and each frame feeds that
// frame's recorded input; otherwise nothing is pressed. Each frame: before_step(dt) if set, scene.update(dt),
// physics.step, then the state hash. Errors: frames outside 1..1000000, fixed_hz outside 1..1000.
struct ARCANE_API FixedRunOptions {
  int frames = 60;
  int fixed_hz = 60;
  int seed = 0;
  const ReplayRecording* recording = nullptr;
  std::function<void(float)> before_step;
};

ARCANE_API std::expected<std::vector<std::uint64_t>, std::string> run_fixed_frames(Scene& scene,
                                                                                 const ComponentRegistry& registry,
                                                                                 InputActionMap& actions,
                                                                                 const FixedRunOptions& options);

// Index of the first frame where the two traces differ (a length mismatch counts at the shorter length);
// nullopt when identical.
ARCANE_API std::optional<int> first_divergent_frame(const std::vector<std::uint64_t>& a,
                                                    const std::vector<std::uint64_t>& b);

// Trace file `arcane.replay_trace` v1: {format, version, hashes: ["16 hex digits", ...]}.
ARCANE_API std::expected<std::string, std::string> save_trace_json(const std::vector<std::uint64_t>& hashes);
ARCANE_API std::expected<std::vector<std::uint64_t>, std::string> load_trace_json(const std::string& text);
ARCANE_API std::string hash_to_hex(std::uint64_t hash);

}  // namespace arcane
