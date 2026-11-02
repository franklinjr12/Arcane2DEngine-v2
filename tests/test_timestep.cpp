#include "arcane/time.hpp"
#include "test_assert.hpp"

#include <cmath>

int main() {
  arcane::FixedTimestep timestep(60.0, 5);
  ARCANE_ASSERT_TRUE(timestep.fixed_delta_seconds() > 0.0166 && timestep.fixed_delta_seconds() < 0.0167);

  // Exactly one fixed step worth of time should yield exactly one step.
  ARCANE_ASSERT_TRUE(timestep.advance(1.0 / 60.0) == 1);
  ARCANE_ASSERT_TRUE(timestep.accumulator_seconds() < 1e-9);

  // Half a step accumulates but does not fire yet.
  ARCANE_ASSERT_TRUE(timestep.advance(1.0 / 120.0) == 0);
  ARCANE_ASSERT_TRUE(timestep.advance(1.0 / 120.0) == 1);

  // A huge delta (e.g. a debugger pause) is capped at max_steps_per_frame
  // and does not accumulate an unbounded backlog.
  arcane::FixedTimestep capped(60.0, 5);
  ARCANE_ASSERT_TRUE(capped.advance(10.0) == 5);
  ARCANE_ASSERT_TRUE(capped.accumulator_seconds() == 0.0);

  // 5 s hitch: clamp, drop the excess, count it, no spiral afterwards.
  arcane::FixedTimestep hitch(60.0, 5);
  ARCANE_ASSERT_TRUE(hitch.advance(5.0) == 5);
  ARCANE_ASSERT_TRUE(hitch.clamped_frames() == 1);
  ARCANE_ASSERT_TRUE(hitch.dropped_seconds() > 4.9 && hitch.dropped_seconds() < 4.92);
  ARCANE_ASSERT_TRUE(hitch.advance(1.0 / 60.0) == 1);

  // Alpha stays in [0, 1) across many irregular frames.
  arcane::FixedTimestep a(60.0, 5);
  const double deltas[] = {0.001, 0.0166, 0.0167, 0.02, 0.0, 1.0 / 60.0, 0.3, 0.0001};
  for (int i = 0; i < 200; ++i) {
    a.advance(deltas[i % 8]);
    ARCANE_ASSERT_TRUE(a.alpha() >= 0.0 && a.alpha() < 1.0);
  }

  // time_scale 0: no steps, accumulator untouched, alpha 0.
  arcane::FixedTimestep paused(60.0, 5);
  ARCANE_ASSERT_TRUE(paused.set_time_scale(0.0));
  for (int i = 0; i < 10; ++i) ARCANE_ASSERT_TRUE(paused.advance(0.1) == 0);
  ARCANE_ASSERT_TRUE(paused.alpha() == 0.0);

  // time_scale 2: double steps (deterministic: 1/60 s frames -> 2 steps each).
  arcane::FixedTimestep fast(60.0, 5);
  ARCANE_ASSERT_TRUE(fast.set_time_scale(2.0));
  int total = 0;
  for (int i = 0; i < 30; ++i) total += fast.advance(1.0 / 60.0);
  ARCANE_ASSERT_TRUE(total == 60);

  // Invalid values are rejected and leave state unchanged.
  arcane::FixedTimestep bad(60.0, 5);
  ARCANE_ASSERT_TRUE(bad.advance(-1.0) == 0);
  ARCANE_ASSERT_TRUE(bad.advance(std::nan("")) == 0);
  ARCANE_ASSERT_TRUE(bad.advance(INFINITY) == 0);
  ARCANE_ASSERT_TRUE(bad.rejected_deltas() == 3);
  ARCANE_ASSERT_TRUE(bad.accumulator_seconds() == 0.0);
  ARCANE_ASSERT_TRUE(!bad.set_time_scale(-1.0));
  ARCANE_ASSERT_TRUE(!bad.set_time_scale(std::nan("")));
  ARCANE_ASSERT_TRUE(bad.time_scale() == 1.0);

  return 0;
}
