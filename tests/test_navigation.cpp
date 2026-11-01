#include <cmath>
#include <memory>
#include <vector>

#include "arcane/component_registry.hpp"
#include "arcane/navigation.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "test_assert.hpp"

namespace {

using arcane::NavGrid;
using arcane::Vec2;

constexpr float kDt = 1.0f / 60.0f;

NavGrid make_grid(int w, int h, float cell = 10.0f) {
  NavGrid g;
  g.width = w;
  g.height = h;
  g.cell_size = {cell, cell};
  g.blocked.assign(static_cast<std::size_t>(w * h), 0);
  return g;
}

void block(NavGrid& g, int cx, int cy) { g.blocked[static_cast<std::size_t>(cy * g.width + cx)] = 1; }

float len(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

bool path_clear(const NavGrid& g, const std::vector<Vec2>& path) {
  for (Vec2 p : path) {
    int cx = 0, cy = 0;
    if (!g.world_to_cell(p, cx, cy) || g.is_blocked(cx, cy)) return false;
  }
  return true;
}

void test_straight_path() {
  const NavGrid g = make_grid(10, 10);
  const auto path = arcane::find_path(g, {5.0f, 5.0f}, {95.0f, 5.0f});
  ARCANE_ASSERT_TRUE(path.size() == 9);
  ARCANE_ASSERT_TRUE(path.front() == (Vec2{15.0f, 5.0f}));
  ARCANE_ASSERT_TRUE(path.back() == (Vec2{95.0f, 5.0f}));
  const auto same = arcane::find_path(g, {1.0f, 1.0f}, {8.0f, 3.0f});
  ARCANE_ASSERT_TRUE(same.size() == 1 && same[0] == (Vec2{8.0f, 3.0f}));
  ARCANE_ASSERT_TRUE(arcane::find_path(g, {-5.0f, 5.0f}, {95.0f, 5.0f}).empty());
  ARCANE_ASSERT_TRUE(arcane::find_path(g, {5.0f, 5.0f}, {195.0f, 5.0f}).empty());
}

void test_around_wall() {
  NavGrid g = make_grid(10, 10);
  for (int y = 0; y < 9; ++y) block(g, 5, y);  // gap only at the bottom row
  const auto path = arcane::find_path(g, {5.0f, 5.0f}, {95.0f, 5.0f});
  ARCANE_ASSERT_TRUE(!path.empty());
  ARCANE_ASSERT_TRUE(path_clear(g, path));
  ARCANE_ASSERT_TRUE(path.back() == (Vec2{95.0f, 5.0f}));
  bool through_gap = false;
  for (Vec2 p : path) through_gap = through_gap || (p.x > 50.0f && p.x < 60.0f && p.y > 90.0f);
  ARCANE_ASSERT_TRUE(through_gap);
}

void test_no_corner_cutting() {
  NavGrid g = make_grid(3, 3);
  block(g, 1, 0);  // diagonal (0,0)->(1,1) would squeeze past this corner
  const auto path = arcane::find_path(g, {5.0f, 5.0f}, {15.0f, 15.0f});
  ARCANE_ASSERT_TRUE(path.size() == 2);  // around via (0,1), never the diagonal
  ARCANE_ASSERT_TRUE(path[0] == g.cell_center(0, 1));
  // Every diagonal step in a longer path has both orthogonal neighbours free.
  NavGrid h = make_grid(8, 8);
  for (int i = 1; i < 7; ++i) block(h, i, i);
  const auto p = arcane::find_path(h, h.cell_center(0, 7), h.cell_center(7, 0));
  ARCANE_ASSERT_TRUE(!p.empty() && path_clear(h, p));
  Vec2 prev = h.cell_center(0, 7);
  for (Vec2 q : p) {
    int ax, ay, bx, by;
    h.world_to_cell(prev, ax, ay);
    h.world_to_cell(q, bx, by);
    if (ax != bx && ay != by) ARCANE_ASSERT_TRUE(!h.is_blocked(bx, ay) && !h.is_blocked(ax, by));
    prev = q;
  }
  arcane::PathOptions no_diag;
  no_diag.allow_diagonal = false;
  const auto four = arcane::find_path(g, {5.0f, 5.0f}, {15.0f, 15.0f}, no_diag);
  ARCANE_ASSERT_TRUE(four.size() == 2);
}

void test_unreachable_and_blocked_goal() {
  NavGrid g = make_grid(10, 10);
  for (int y = 0; y < 10; ++y) block(g, 5, y);
  ARCANE_ASSERT_TRUE(arcane::find_path(g, {5.0f, 5.0f}, {95.0f, 5.0f}).empty());
  NavGrid h = make_grid(10, 10);
  block(h, 9, 0);
  ARCANE_ASSERT_TRUE(arcane::find_path(h, {5.0f, 5.0f}, {95.0f, 5.0f}).empty());
}

void test_deterministic() {
  NavGrid g = make_grid(12, 12);
  for (int i = 2; i < 10; ++i) block(g, i, 5);
  const auto a = arcane::find_path(g, {5.0f, 5.0f}, {115.0f, 115.0f});
  const auto b = arcane::find_path(g, {5.0f, 5.0f}, {115.0f, 115.0f});
  ARCANE_ASSERT_TRUE(!a.empty() && a == b);
}

void test_from_tilemap() {
  arcane::ComponentRegistry registry;
  arcane::register_tilemap_components(registry);
  arcane::Scene scene;
  arcane::Node* n = scene.create_node("map");
  n->local_transform().position = {100.0f, 50.0f};
  auto* map = static_cast<arcane::Tilemap*>(scene.add_component(n, "Tilemap", registry));
  map->width = 4;
  map->height = 3;
  map->tile_width = 16.0f;
  map->tile_height = 8.0f;
  map->solid_tiles = "2";
  map->add_layer();
  map->add_layer();
  map->set_tile(0, 1, 0, 2);
  map->set_tile(1, 3, 2, 2);
  map->set_tile(0, 2, 1, 1);  // not solid
  const NavGrid g = NavGrid::from_tilemap(*n, *map);
  ARCANE_ASSERT_TRUE(g.width == 4 && g.height == 3);
  ARCANE_ASSERT_TRUE(g.origin == (Vec2{100.0f, 50.0f}) && g.cell_size == (Vec2{16.0f, 8.0f}));
  ARCANE_ASSERT_TRUE(g.is_blocked(1, 0) && g.is_blocked(3, 2));
  ARCANE_ASSERT_TRUE(!g.is_blocked(2, 1) && !g.is_blocked(0, 0));
  ARCANE_ASSERT_TRUE(g.is_blocked(-1, 0) && g.is_blocked(4, 0));
  int cx = 0, cy = 0;
  ARCANE_ASSERT_TRUE(g.world_to_cell({117.0f, 51.0f}, cx, cy) && cx == 1 && cy == 0);
  ARCANE_ASSERT_TRUE(g.cell_center(1, 0) == (Vec2{124.0f, 54.0f}));
}

void test_simplify() {
  const NavGrid g = make_grid(10, 10);
  const std::vector<Vec2> path{{15, 5}, {25, 5}, {35, 5}, {45, 15}, {55, 25}, {55, 35}};
  const auto s = arcane::simplify_path(g, path);
  const std::vector<Vec2> expected{{15, 5}, {35, 5}, {55, 25}, {55, 35}};
  ARCANE_ASSERT_TRUE(s == expected);
  ARCANE_ASSERT_TRUE(arcane::simplify_path(g, {{1, 1}, {2, 2}}).size() == 2);
}

void test_steering_functions() {
  const Vec2 far = arcane::arrive({0, 0}, {100, 0}, 90.0f, 48.0f);
  ARCANE_ASSERT_TRUE(std::fabs(far.x - 90.0f) < 1e-4f);
  const Vec2 inside = arcane::arrive({0, 0}, {24, 0}, 90.0f, 48.0f);
  ARCANE_ASSERT_TRUE(std::fabs(inside.x - 45.0f) < 1e-4f);
  ARCANE_ASSERT_TRUE(arcane::arrive({0, 0}, {0, 0}, 90.0f, 48.0f) == Vec2{});
  const Vec2 s = arcane::seek({0, 0}, {0, 5}, 80.0f);
  ARCANE_ASSERT_TRUE(std::fabs(s.y - 80.0f) < 1e-4f && s.x == 0.0f);
  const Vec2 f = arcane::flee({10, 10}, {0, 10}, 60.0f);
  ARCANE_ASSERT_TRUE(f.x > 59.0f && std::fabs(f.y) < 1e-4f);
  const Vec2 sep = arcane::separation({0, 0}, {{5, 0}, {100, 0}}, 20.0f, 50.0f);
  ARCANE_ASSERT_TRUE(sep.x < 0.0f && len(sep) <= 50.0f + 1e-3f);
  ARCANE_ASSERT_TRUE(arcane::separation({0, 0}, {{100, 0}}, 20.0f, 50.0f) == Vec2{});
}

// 12x7 tiles of 32 px; a wall at column 6 with a gap in the top row. Agent on the left, static target on the right.
void test_pathfinding_sim() {
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
  for (int y = 1; y < 7; ++y) map->set_tile(0, 6, y, 1);

  arcane::Node* goal = scene.create_node("goal");
  goal->local_transform().position = {11.0f * 32.0f + 16.0f, 6.0f * 32.0f + 16.0f};

  arcane::Node* agent = scene.create_node("agent");
  agent->local_transform().position = {16.0f, 6.0f * 32.0f + 16.0f};
  auto* body = static_cast<arcane::RigidBody2D*>(scene.add_component(agent, "RigidBody2D", registry));
  body->fixed_rotation = true;
  body->gravity_scale = 0.0f;
  auto* collider = static_cast<arcane::Collider2D*>(scene.add_component(agent, "Collider2D", registry));
  collider->width = 16.0f;
  collider->height = 16.0f;
  collider->friction = 0.0f;
  auto* steer = static_cast<arcane::SteeringAgent2D*>(scene.add_component(agent, "SteeringAgent2D", registry));
  steer->target = "goal";
  steer->behavior = arcane::SteeringAgent2D::Arrive;
  steer->max_speed = 120.0f;
  physics.rebuild(scene);

  float best = 1e9f;
  for (int i = 0; i < 20 * 60; ++i) {
    scene.update(kDt);
    physics.step(scene, kDt);
    const Vec2 p = agent->local_transform().position;
    best = std::min(best, len({p.x - goal->local_transform().position.x, p.y - goal->local_transform().position.y}));
    if (best < 16.0f) break;
  }
  ARCANE_ASSERT_TRUE(best < 16.0f);
  ARCANE_ASSERT_TRUE(!steer->path().empty());
}

}  // namespace

int main() {
  test_straight_path();
  test_around_wall();
  test_no_corner_cutting();
  test_unreachable_and_blocked_goal();
  test_deterministic();
  test_from_tilemap();
  test_simplify();
  test_steering_functions();
  test_pathfinding_sim();
  return 0;
}
