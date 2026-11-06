#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "arcane/component_registry.hpp"
#include "arcane/navigation.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "test_assert.hpp"

namespace {

using arcane::Vec2;

constexpr float kDt = 1.0f / 60.0f;
// Per-step budget for separation_steering over 300 agents; keep in sync with docs/perf.md.
constexpr double kBudgetMs = 20.0;

float len(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

std::vector<arcane::SeparationAgent> make_grid_agents() {
  std::vector<arcane::SeparationAgent> agents;
  for (int y = 0; y < 15; ++y) {
    for (int x = 0; x < 20; ++x) {
      const float jx = static_cast<float>((x * 7 + y * 13) % 5) * 0.3f;
      const float jy = static_cast<float>((x * 11 + y * 3) % 5) * 0.3f;
      agents.push_back({{x * 30.0f + jx, y * 30.0f + jy}, 0});
    }
  }
  return agents;
}

void test_separation_converging() {
  auto agents = make_grid_agents();
  ARCANE_ASSERT_TRUE(agents.size() == 300);
  const Vec2 centre{285.0f, 210.0f};
  // Query radius 24; the asserted spacing is the agent radius (12): agents must not overlap.
  constexpr float kQueryRadius = 36.0f;
  constexpr float kAgentRadius = 12.0f;
  std::vector<Vec2> sep;
  for (int step = 0; step < 300; ++step) {
    arcane::separation_steering(agents, kQueryRadius, 120.0f, sep);
    for (std::size_t i = 0; i < agents.size(); ++i) {
      const Vec2 s = arcane::arrive(agents[i].position, centre, 60.0f, 60.0f);
      agents[i].position.x += (s.x + 2.0f * sep[i].x) * kDt;
      agents[i].position.y += (s.y + 2.0f * sep[i].y) * kDt;
    }
  }
  float min_d = 1e9f;
  float max_centre = 0.0f;
  for (std::size_t i = 0; i < agents.size(); ++i) {
    max_centre = std::max(max_centre, len({agents[i].position.x - centre.x, agents[i].position.y - centre.y}));
    for (std::size_t j = i + 1; j < agents.size(); ++j) {
      min_d = std::min(min_d, len({agents[i].position.x - agents[j].position.x,
                                   agents[i].position.y - agents[j].position.y}));
    }
  }
  std::printf("converging: min spacing %.2f, max distance to centre %.1f\n", min_d, max_centre);
  ARCANE_ASSERT_TRUE(min_d >= kAgentRadius);
  ARCANE_ASSERT_TRUE(max_centre < 250.0f);  // they did converge

  // Different layers ignore each other, coincident same-layer agents separate without NaN.
  std::vector<arcane::SeparationAgent> two{{{5.0f, 5.0f}, 0}, {{5.0f, 5.0f}, 1}};
  arcane::separation_steering(two, 24.0f, 100.0f, sep);
  ARCANE_ASSERT_TRUE(sep.size() == 2 && sep[0].x == 0.0f && sep[0].y == 0.0f && sep[1].x == 0.0f && sep[1].y == 0.0f);
  two[1].layer = 0;
  arcane::separation_steering(two, 24.0f, 100.0f, sep);
  ARCANE_ASSERT_TRUE(std::isfinite(sep[0].x) && std::isfinite(sep[0].y) && len(sep[0]) > 1.0f);
  ARCANE_ASSERT_TRUE(std::fabs(sep[0].x + sep[1].x) < 1e-3f && std::fabs(sep[0].y + sep[1].y) < 1e-3f);
  ARCANE_ASSERT_TRUE(len(sep[0]) <= 100.0f + 1e-3f);
}

void test_separation_budget() {
  auto agents = make_grid_agents();
  std::vector<Vec2> sep;
  constexpr int kSteps = 300;
  const auto t0 = std::chrono::steady_clock::now();
  for (int step = 0; step < kSteps; ++step) {
    arcane::separation_steering(agents, 24.0f, 180.0f, sep);
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agents[i].position.x += sep[i].x * kDt;
      agents[i].position.y += sep[i].y * kDt;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double avg_ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / kSteps;
  std::printf("separation_steering: 300 agents, avg %.4f ms/step (budget %.2f ms)\n", avg_ms, kBudgetMs);
  ARCANE_ASSERT_TRUE(avg_ms < kBudgetMs);
}

void test_line_of_sight() {
  arcane::ComponentRegistry registry;
  arcane::register_tilemap_components(registry);
  arcane::register_physics_components(registry);
  arcane::Scene scene;
  arcane::PhysicsWorld physics;

  arcane::Node* ground = scene.create_node("ground");
  auto* map = static_cast<arcane::Tilemap*>(scene.add_component(ground, "Tilemap", registry));
  map->width = 12;
  map->height = 7;
  map->tile_width = 32.0f;
  map->tile_height = 32.0f;
  map->collide = true;
  map->solid_tiles = "1";
  map->add_layer();
  for (int y = 0; y < 7; ++y) map->set_tile(0, 6, y, 1);
  physics.rebuild(scene);

  const Vec2 left{80.0f, 100.0f};
  const Vec2 right{300.0f, 100.0f};
  ARCANE_ASSERT_TRUE(!arcane::line_of_sight(physics, left, right));
  ARCANE_ASSERT_TRUE(arcane::line_of_sight(physics, left, {120.0f, 150.0f}));
  ARCANE_ASSERT_TRUE(arcane::line_of_sight(physics, right, {340.0f, 20.0f}));
  ARCANE_ASSERT_TRUE(arcane::line_of_sight(physics, left, left));
  // A mask that excludes the tilemap's collision layer does not see the wall.
  ARCANE_ASSERT_TRUE(arcane::line_of_sight(physics, left, right, 1 << 5));

  // The viewer's own body, when ignored, does not block.
  arcane::Node* viewer = scene.create_node("viewer");
  viewer->local_transform().position = left;
  scene.add_component(viewer, "RigidBody2D", registry);
  auto* collider = static_cast<arcane::Collider2D*>(scene.add_component(viewer, "Collider2D", registry));
  collider->width = 16.0f;
  collider->height = 16.0f;
  for (int y = 0; y < 7; ++y) map->set_tile(0, 6, y, 0);
  physics.rebuild(scene);
  ARCANE_ASSERT_TRUE(arcane::line_of_sight(physics, {left.x - 40.0f, left.y}, right, arcane::kAllCollisionLayers,
                                           viewer->id()));
  ARCANE_ASSERT_TRUE(!arcane::line_of_sight(physics, {left.x - 40.0f, left.y}, right));
  ARCANE_ASSERT_TRUE(arcane::line_of_sight(physics, left, right, arcane::kAllCollisionLayers, viewer->id()));
}

void test_keep_distance_kiter() {
  constexpr float kMin = 80.0f;
  constexpr float kMax = 160.0f;
  Vec2 target{0.0f, 0.0f};
  Vec2 kiter{220.0f, 0.0f};  // 120 px from the target's first position (100, 0)
  float lo = 1e9f;
  float hi = 0.0f;
  for (int step = 0; step < 600; ++step) {
    const float t = static_cast<float>(step) * kDt;
    // The kiter steers with a band 10 px tighter than the asserted limits (proportional response lags the target).
    // Target circles with radius 100 at 40 px/s.
    target = {100.0f * std::cos(t * 0.4f), 100.0f * std::sin(t * 0.4f)};
    const Vec2 v = arcane::keep_distance(kiter, target, kMin + 10.0f, kMax - 10.0f, 120.0f);
    ARCANE_ASSERT_TRUE(std::isfinite(v.x) && std::isfinite(v.y) && len(v) <= 120.0f + 1e-3f);
    kiter.x += v.x * kDt;
    kiter.y += v.y * kDt;
    const float d = len({kiter.x - target.x, kiter.y - target.y});
    lo = std::min(lo, d);
    hi = std::max(hi, d);
    ARCANE_ASSERT_TRUE(d >= kMin && d <= kMax);
  }
  std::printf("kiter distance range [%.1f, %.1f]\n", lo, hi);

  const Vec2 o{0.0f, 0.0f};
  Vec2 v = arcane::keep_distance({120.0f, 0.0f}, o, kMin, kMax, 100.0f);
  ARCANE_ASSERT_TRUE(v.x == 0.0f && v.y == 0.0f);
  v = arcane::keep_distance({40.0f, 0.0f}, o, kMin, kMax, 100.0f);
  ARCANE_ASSERT_TRUE(v.x > 0.0f);  // too close: moves away from the target (target is at -x)
  v = arcane::keep_distance({300.0f, 0.0f}, o, kMin, kMax, 100.0f);
  ARCANE_ASSERT_TRUE(v.x < 0.0f && len(v) <= 100.0f);  // too far: moves closer
  v = arcane::keep_distance(o, o, kMin, kMax, 100.0f);
  ARCANE_ASSERT_TRUE(std::isfinite(v.x) && std::isfinite(v.y) && v.x > 0.0f);  // dist 0: flees along +x
  v = arcane::keep_distance({50.0f, 0.0f}, o, 200.0f, 100.0f, 100.0f);  // min > max
  ARCANE_ASSERT_TRUE(std::isfinite(v.x) && std::isfinite(v.y));
}

}  // namespace

int main() {
  test_separation_converging();
  test_separation_budget();
  test_line_of_sight();
  test_keep_distance_kiter();
  return 0;
}
