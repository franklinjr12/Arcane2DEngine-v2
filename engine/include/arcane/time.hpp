#pragma once

#include <cmath>

namespace arcane {

// Accumulator-based fixed-timestep helper. Feed it the real frame delta via
// advance(); it returns how many fixed steps to run this frame. Rendering is
// expected to happen once per frame regardless of how many fixed steps ran.
// Pure logic, no platform/GL dependency, so it is directly unit-testable.
//
// Time scale and pause: the real delta is multiplied by time_scale() before it
// is added to the accumulator. Scale 0.5 yields half as many steps, 2 double,
// 0 no steps (accumulator and alpha() frozen, rendering continues). Large
// scales remain bounded by max_steps_per_frame and the clamp drop rule, so
// they cannot cause a spiral. fixed_delta_seconds() is never scaled, which
// keeps simulation determinism. While paused, valid deltas add nothing to the
// accumulator and advance() returns 0, so accumulator and alpha() stay frozen
// and resume() does not catch up on the paused time. Code that must ignore
// pause and scale (UI, tweens) should use unscaled_delta_seconds().
class FixedTimestep {
 public:
  explicit FixedTimestep(double hz = 60.0, int max_steps_per_frame = 5) noexcept
      : fixed_dt_(1.0 / hz), max_steps_(max_steps_per_frame) {}

  // Returns the number of fixed steps that should run for this frame.
  // Invalid deltas are rejected (counted, no state change) even while paused.
  // While paused a valid delta only updates the unscaled time and returns 0.
  // If the frame took too long (e.g. a debugger pause), steps are capped at
  // max_steps_per_frame and the remaining accumulated time is discarded to
  // avoid a spiral of death.
  int advance(double frame_delta_seconds) noexcept {
    // Negative, NaN or infinite deltas are rejected: no steps, no state change.
    if (!(frame_delta_seconds >= 0.0) || !std::isfinite(frame_delta_seconds)) {
      ++rejected_deltas_;
      return 0;
    }
    unscaled_delta_ = frame_delta_seconds;
    unscaled_elapsed_ += frame_delta_seconds;
    if (paused_) return 0;
    scaled_elapsed_ += frame_delta_seconds * time_scale_;
    accumulator_ += frame_delta_seconds * time_scale_;
    int steps = 0;
    while (accumulator_ >= fixed_dt_ && steps < max_steps_) {
      accumulator_ -= fixed_dt_;
      ++steps;
    }
    if (steps == max_steps_) {
      if (accumulator_ > 0.0) {
        dropped_seconds_ += accumulator_;
        ++clamped_frames_;
      }
      accumulator_ = 0.0;
    }
    return steps;
  }

  double fixed_delta_seconds() const noexcept { return fixed_dt_; }
  double accumulator_seconds() const noexcept { return accumulator_; }
  // Fraction of a fixed step already accumulated, useful for render interpolation.
  // Always in [0, 1).
  double alpha() const noexcept {
    const double a = accumulator_ / fixed_dt_;
    if (!(a > 0.0)) return 0.0;
    return a < 1.0 ? a : std::nextafter(1.0, 0.0);
  }

  // Pause: advance() returns 0 and the accumulator/alpha() stay frozen.
  // Does not change time_scale(); resuming does not catch up paused time.
  void pause() noexcept { paused_ = true; }
  void resume() noexcept { paused_ = false; }
  void set_paused(bool p) noexcept { paused_ = p; }
  bool paused() const noexcept { return paused_; }

  // Real (unscaled) time from valid deltas, regardless of pause or time scale.
  double unscaled_elapsed_seconds() const noexcept { return unscaled_elapsed_; }
  // The last valid real delta passed to advance().
  double unscaled_delta_seconds() const noexcept { return unscaled_delta_; }
  // Sum of delta * time_scale over valid deltas while not paused.
  double scaled_elapsed_seconds() const noexcept { return scaled_elapsed_; }

  // Scales real time before accumulation: 0 = no steps (rendering continues),
  // 2 = double steps. May be changed while paused; applies after resume. Negative or non-finite values are ignored.
  // Returns true if the value was accepted.
  bool set_time_scale(double scale) noexcept {
    if (!(scale >= 0.0) || !std::isfinite(scale)) return false;
    time_scale_ = scale;
    return true;
  }
  double time_scale() const noexcept { return time_scale_; }

  // Diagnostics: simulated time discarded by the step clamp, frames that hit
  // the clamp with time left over, and invalid deltas rejected.
  double dropped_seconds() const noexcept { return dropped_seconds_; }
  int clamped_frames() const noexcept { return clamped_frames_; }
  int rejected_deltas() const noexcept { return rejected_deltas_; }

 private:
  double fixed_dt_;
  double accumulator_ = 0.0;
  int max_steps_;
  double time_scale_ = 1.0;
  bool paused_ = false;
  double unscaled_elapsed_ = 0.0;
  double unscaled_delta_ = 0.0;
  double scaled_elapsed_ = 0.0;
  double dropped_seconds_ = 0.0;
  int clamped_frames_ = 0;
  int rejected_deltas_ = 0;
};

}  // namespace arcane
