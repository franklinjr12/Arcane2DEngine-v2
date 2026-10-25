#include "arcane/time.hpp"
#include "test_assert.hpp"

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

  return 0;
}
