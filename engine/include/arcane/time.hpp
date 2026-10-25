#pragma once

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
    accumulator_ += frame_delta_seconds;
    int steps = 0;
    while (accumulator_ >= fixed_dt_ && steps < max_steps_) {
      accumulator_ -= fixed_dt_;
      ++steps;
    }
    if (steps == max_steps_) {
      accumulator_ = 0.0;
    }
    return steps;
  }

  double fixed_delta_seconds() const noexcept { return fixed_dt_; }
  double accumulator_seconds() const noexcept { return accumulator_; }
  // Fraction of a fixed step already accumulated, useful for render interpolation.
  double alpha() const noexcept { return accumulator_ / fixed_dt_; }

 private:
  double fixed_dt_;
  double accumulator_ = 0.0;
  int max_steps_;
};

}  // namespace arcane
