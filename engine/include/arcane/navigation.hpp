#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/node.hpp"
#include "arcane/tilemap.hpp"

namespace arcane {

class ComponentRegistry;

// Uniform walkability grid in world pixels (x right, y down). `origin` is the world position of the top-left corner
// of cell (0, 0); `blocked` is row-major (width*height, non-zero = blocked).
struct ARCANE_API NavGrid {
  int width = 0;
  int height = 0;
  Vec2 cell_size{32.0f, 32.0f};
  Vec2 origin{};
  std::vector<std::uint8_t> blocked;

  bool in_bounds(int cx, int cy) const noexcept { return cx >= 0 && cy >= 0 && cx < width && cy < height; }
  // Out-of-bounds cells count as blocked.
  bool is_blocked(int cx, int cy) const noexcept;
  // Cell containing `world`; false (cx/cy still set) if outside the grid.
  bool world_to_cell(Vec2 world, int& cx, int& cy) const noexcept;
  Vec2 cell_center(int cx, int cy) const noexcept;

  // A cell is blocked when any layer has a solid tile (Tilemap::is_solid) there. Uses the node's world position and
  // scale like Tilemap::world_to_cell; rotation is ignored. An empty grid for a degenerate (zero) scale or size.
  static NavGrid from_tilemap(const Node& tilemap_node, const Tilemap& tilemap);
};

struct PathOptions {
  bool allow_diagonal = true;
  int max_expansions = 20000;
};

// Deterministic grid A* (octile distance, costs 1 and sqrt(2), no diagonal corner cutting, ties broken by lower h then
// lower cell index). Returns cell-center waypoints from the cell after `start` to the goal cell, the last one replaced
// by the exact `goal`. Empty if start/goal is out of bounds, the goal is blocked, no path exists or max_expansions is
// hit. Start and goal in the same cell give {goal}.
ARCANE_API std::vector<Vec2> find_path(const NavGrid& grid, Vec2 start, Vec2 goal, const PathOptions& options = {});

// Removes waypoints that lie on a straight line between their neighbours.
ARCANE_API std::vector<Vec2> simplify_path(const NavGrid& grid, std::vector<Vec2> path);

// Steering behaviours: each returns a desired velocity in px/s.
ARCANE_API Vec2 seek(Vec2 pos, Vec2 target, float max_speed) noexcept;
ARCANE_API Vec2 flee(Vec2 pos, Vec2 threat, float max_speed) noexcept;
// Like seek, but slows linearly inside `slow_radius` and stops at the target.
ARCANE_API Vec2 arrive(Vec2 pos, Vec2 target, float max_speed, float slow_radius) noexcept;
// Pushes away from neighbours closer than `radius` (stronger when closer); length is at most max_speed.
ARCANE_API Vec2 separation(Vec2 pos, const std::vector<Vec2>& neighbours, float radius, float max_speed) noexcept;

// Moves the sibling RigidBody2D toward a target node (or a position set from code) with seek/arrive/flee, following
// a grid A* path over a Tilemap's solid cells when use_pathfinding is set. Runs in on_update (fixed step, before
// PhysicsWorld::step); does nothing without a sibling RigidBody2D. Give the body gravity_scale 0.
class ARCANE_API SteeringAgent2D : public Component {
 public:
  enum Behavior { Seek = 0, Arrive = 1, Flee = 2 };

  SteeringAgent2D();
  ~SteeringAgent2D() override;

  std::string target;           // node name; ignored while a position override is set
  int behavior = Seek;
  float max_speed = 90.0f;           // px/s
  float max_acceleration = 600.0f;   // px/s^2
  float slow_radius = 48.0f;         // arrive only
  float stop_distance = 4.0f;        // seek/arrive stop (zero desired velocity) this close to the target
  bool use_pathfinding = true;       // seek/arrive follow a grid path; flee never does
  float repath_interval = 0.5f;      // seconds between path rebuilds
  std::string tilemap;               // node name; empty = first node with a Tilemap
  bool allow_diagonal = true;

  void on_update(float fixed_dt) override;

  // Runtime state, never serialized.
  const std::vector<Vec2>& path() const noexcept { return path_; }
  void set_target_position(Vec2 position) noexcept;  // overrides the target node
  void clear_target_position() noexcept { has_target_position_ = false; }
  // Drops the cached NavGrid and path (e.g. after editing tiles at runtime); the grid is otherwise only rebuilt
  // when the tilemap node changes.
  void invalidate_navigation() noexcept;

 private:
  bool resolve_target(Vec2& out) const;
  void repath(Vec2 position, Vec2 goal);

  std::vector<Vec2> path_;
  std::size_t path_index_ = 0;
  float repath_timer_ = 0.0f;
  bool has_target_position_ = false;
  Vec2 target_position_{};
  NavGrid grid_;
  NodeId grid_node_ = kInvalidNodeId;
  const Tilemap* grid_tilemap_ = nullptr;
  bool has_grid_ = false;
};

ARCANE_API void register_navigation_components(ComponentRegistry& registry);

}  // namespace arcane
