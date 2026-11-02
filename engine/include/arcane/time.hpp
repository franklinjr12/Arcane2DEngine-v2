#pragma once

#include <cmath>

namespace arcane {

// Accumulator-based fixed-timestep helper. Feed it the real frame delta via
// advance(); it returns how many fixed steps to run this frame. Rendering is
// expected to happen once per frame regardless of how many fixed steps ran.
// Pure logic, no platform/GL dependency, so it is directly unit-testable.
class FixedTimestep {
 public:
  explicit FixedTimestep(double hz = 60.0, int max_steps_per_frame = 5) noexcept
      : fixed_dt_(1.0 / hz), max_steps_(max_steps_per_frame) {}

  // Returns the number of fixed steps that should run for this frame.
  // If the frame took too long (e.g. a debugger pause), steps are capped at
  // max_steps_per_frame and the remaining accumulated time is discarded to
  // avoid a spiral of death.
  int advance(double frame_delta_seconds) noexcept {
    // Negative, NaN or infinite deltas are rejected: no steps, no state change.
    if (!(frame_delta_seconds >= 0.0) || !std::isfinite(frame_delta_seconds)) {
      ++rejected_deltas_;
      return 0;
    }
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

  // Scales real time before accumulation: 0 = no steps (rendering continues),
  // 2 = double steps. Negative or non-finite values are ignored.
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
  double dropped_seconds_ = 0.0;
  int clamped_frames_ = 0;
  int rejected_deltas_ = 0;
};

}  // namespace arcane
