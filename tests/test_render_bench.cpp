// GL render benchmark on the PF1 scene: batching and culling regression budget (counts only, no timings).
// Needs a GL context; exits 77 (CTest skip) without one.
#include <cstdio>

#include "arcane_editor/render_bench.hpp"
#include "test_assert.hpp"

int main() {
  const arcane::BenchConfig config{.sprites = 5000, .emitters = 20, .particles_per_emitter = 100, .bodies = 500, .frames = 3};
  auto result = arcane::editor::run_render_benchmark(config);
  if (!result) {
    std::fprintf(stderr, "render benchmark unavailable, skipping: %s\n", result.error().c_str());
    return 77;
  }
  ARCANE_ASSERT_TRUE(result->live_particles == 2000);
  ARCANE_ASSERT_TRUE(result->physics_bodies == 501);
  // Every quad is either submitted or culled: 5000 sprites + 2000 particles.
  ARCANE_ASSERT_TRUE(result->sprites + result->culled == 7000);
  // The 1600 x 800 sprite grid is larger than the 1280 x 720 view, so culling must drop some.
  ARCANE_ASSERT_TRUE(result->culled > 0);
  std::fprintf(stderr, "draw_calls=%d sprites=%d culled=%d\n", result->draw_calls, result->sprites, result->culled);
  // Budget: observed 2 calls (one batch for the 1x1 sprite texture, one for the particles' white texture; each
  // texture change breaks a batch). One spare call of margin; a per-sprite or per-emitter draw would blow past it.
  ARCANE_ASSERT_TRUE(result->draw_calls >= 1 && result->draw_calls <= 3);
  return 0;
}
