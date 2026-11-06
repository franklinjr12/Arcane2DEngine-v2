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

  // (a) time_scale 0.5: half as many steps.
  arcane::FixedTimestep half(60.0, 5);
  ARCANE_ASSERT_TRUE(half.set_time_scale(0.5));
  int half_total = 0;
  for (int i = 0; i < 60; ++i) half_total += half.advance(1.0 / 60.0);
  // 0.5 * (1/60) is exactly half of fixed_dt here, so the accumulated
  // total reaches 30 full steps (verified by running).
  ARCANE_ASSERT_TRUE(half_total == 30);

  // (b) time_scale 0: unscaled time advances, scaled time does not.
  arcane::FixedTimestep zero(60.0, 5);
  ARCANE_ASSERT_TRUE(zero.set_time_scale(0.0));
  for (int i = 0; i < 10; ++i) zero.advance(0.1);
  ARCANE_ASSERT_TRUE(std::fabs(zero.unscaled_elapsed_seconds() - 1.0) < 1e-9);
  ARCANE_ASSERT_TRUE(zero.scaled_elapsed_seconds() == 0.0);
  ARCANE_ASSERT_TRUE(zero.unscaled_delta_seconds() == 0.1);

  // (c) pause/resume: frozen alpha, no catch-up spiral.
  arcane::FixedTimestep p(60.0, 5);
  ARCANE_ASSERT_TRUE(!p.paused());
  ARCANE_ASSERT_TRUE(p.advance(1.0 / 120.0) == 0);
  const double alpha0 = p.alpha();
  const double acc0 = p.accumulator_seconds();
  const double un0 = p.unscaled_elapsed_seconds();
  p.pause();
  ARCANE_ASSERT_TRUE(p.paused());
  ARCANE_ASSERT_TRUE(p.time_scale() == 1.0);
  for (int i = 0; i < 100; ++i) ARCANE_ASSERT_TRUE(p.advance(0.1) == 0);
  ARCANE_ASSERT_TRUE(p.alpha() == alpha0);
  ARCANE_ASSERT_TRUE(p.accumulator_seconds() == acc0);
  ARCANE_ASSERT_TRUE(p.clamped_frames() == 0 && p.dropped_seconds() == 0.0);
  ARCANE_ASSERT_TRUE(std::fabs(p.unscaled_elapsed_seconds() - un0 - 10.0) < 1e-6);
  p.resume();
  ARCANE_ASSERT_TRUE(!p.paused());
  ARCANE_ASSERT_TRUE(p.advance(1.0 / 120.0) == 1);
  ARCANE_ASSERT_TRUE(p.advance(1.0 / 60.0) == 1);

  // (d) scale 2 with a huge delta is still capped and the excess dropped.
  arcane::FixedTimestep big(60.0, 5);
  ARCANE_ASSERT_TRUE(big.set_time_scale(2.0));
  ARCANE_ASSERT_TRUE(big.advance(10.0) == 5);
  ARCANE_ASSERT_TRUE(big.accumulator_seconds() == 0.0);
  ARCANE_ASSERT_TRUE(big.clamped_frames() == 1 && big.dropped_seconds() > 19.0);

  // (e) invalid deltas while paused are rejected and counted.
  arcane::FixedTimestep pb(60.0, 5);
  pb.set_paused(true);
  pb.advance(0.5);
  const double unb = pb.unscaled_elapsed_seconds();
  ARCANE_ASSERT_TRUE(pb.advance(-1.0) == 0);
  ARCANE_ASSERT_TRUE(pb.advance(std::nan("")) == 0);
  ARCANE_ASSERT_TRUE(pb.rejected_deltas() == 2);
  ARCANE_ASSERT_TRUE(pb.unscaled_elapsed_seconds() == unb);
  ARCANE_ASSERT_TRUE(pb.unscaled_delta_seconds() == 0.5);

  // (f) set_time_scale while paused is accepted and applies after resume.
  arcane::FixedTimestep ps(60.0, 5);
  ps.pause();
  ARCANE_ASSERT_TRUE(ps.set_time_scale(2.0));
  ARCANE_ASSERT_TRUE(ps.advance(1.0 / 60.0) == 0);
  ps.resume();
  ARCANE_ASSERT_TRUE(ps.advance(1.0 / 60.0) == 2);

  return 0;
}
