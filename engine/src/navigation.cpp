#include "arcane/navigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <queue>

#include "arcane/component_registry.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"

namespace arcane {

namespace {

constexpr float kSqrt2 = 1.41421356237f;

struct OpenEntry {
  float f;
  float h;
  int index;
  float g;
};

// priority_queue is a max-heap, so "greater" means lower priority: lower f first, then lower h, then lower index.
struct OpenOrder {
  bool operator()(const OpenEntry& a, const OpenEntry& b) const noexcept {
    if (a.f != b.f) return a.f > b.f;
    if (a.h != b.h) return a.h > b.h;
    return a.index > b.index;
  }
};

float octile(int dx, int dy, bool diagonal) {
  const float ax = static_cast<float>(std::abs(dx));
  const float ay = static_cast<float>(std::abs(dy));
  if (!diagonal) return ax + ay;
  return (ax + ay) + (kSqrt2 - 2.0f) * std::min(ax, ay);
}

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

const Tilemap* tilemap_of(const Node& node) { return dynamic_cast<const Tilemap*>(node.find_component("Tilemap")); }

const Node* first_tilemap_node(const Node& node) {
  if (tilemap_of(node)) return &node;
  for (const auto& child : node.children()) {
    if (const Node* found = first_tilemap_node(*child)) return found;
  }
  return nullptr;
}

}  // namespace

bool NavGrid::is_blocked(int cx, int cy) const noexcept {
  if (!in_bounds(cx, cy)) return true;
  return blocked[static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx)] != 0;
}

bool NavGrid::world_to_cell(Vec2 world, int& cx, int& cy) const noexcept {
  if (cell_size.x == 0.0f || cell_size.y == 0.0f) {
    cx = cy = 0;
    return false;
  }
  cx = static_cast<int>(std::floor((world.x - origin.x) / cell_size.x));
  cy = static_cast<int>(std::floor((world.y - origin.y) / cell_size.y));
  return in_bounds(cx, cy);
}

Vec2 NavGrid::cell_center(int cx, int cy) const noexcept {
  return {origin.x + (static_cast<float>(cx) + 0.5f) * cell_size.x,
          origin.y + (static_cast<float>(cy) + 0.5f) * cell_size.y};
}

NavGrid NavGrid::from_tilemap(const Node& tilemap_node, const Tilemap& tilemap) {
  NavGrid grid;
  const WorldTransform2D world = tilemap_node.world_transform();
  if (tilemap.width <= 0 || tilemap.height <= 0 || tilemap.tile_width <= 0.0f || tilemap.tile_height <= 0.0f ||
      world.scale.x == 0.0f || world.scale.y == 0.0f) {
    return grid;
  }
  grid.width = tilemap.width;
  grid.height = tilemap.height;
  grid.cell_size = {tilemap.tile_width * world.scale.x, tilemap.tile_height * world.scale.y};
  grid.origin = world.position;
  grid.blocked.assign(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), 0);
  for (int cy = 0; cy < grid.height; ++cy) {
    for (int cx = 0; cx < grid.width; ++cx) {
      for (int layer = 0; layer < tilemap.layer_count(); ++layer) {
        if (tilemap.is_solid(tilemap.tile_at(layer, cx, cy))) {
          grid.blocked[static_cast<std::size_t>(cy) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(cx)] = 1;
          break;
        }
      }
    }
  }
  return grid;
}

std::vector<Vec2> find_path(const NavGrid& grid, Vec2 start, Vec2 goal, const PathOptions& options) {
  int sx = 0, sy = 0, gx = 0, gy = 0;
  if (!grid.world_to_cell(start, sx, sy) || !grid.world_to_cell(goal, gx, gy)) return {};
  if (grid.is_blocked(gx, gy)) return {};
  if (sx == gx && sy == gy) return {goal};

  const int w = grid.width;
  const std::size_t cells = static_cast<std::size_t>(w) * static_cast<std::size_t>(grid.height);
  std::vector<float> best_g(cells, std::numeric_limits<float>::infinity());
  std::vector<int> parent(cells, -1);
  std::vector<std::uint8_t> closed(cells, 0);

  // Fixed neighbour order: N, E, S, W, then NE, SE, SW, NW.
  static constexpr int kDx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
  static constexpr int kDy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
  const int neighbour_count = options.allow_diagonal ? 8 : 4;

  std::priority_queue<OpenEntry, std::vector<OpenEntry>, OpenOrder> open;
  const int start_index = sy * w + sx;
  const int goal_index = gy * w + gx;
  best_g[static_cast<std::size_t>(start_index)] = 0.0f;
  const float h0 = octile(gx - sx, gy - sy, options.allow_diagonal);
  open.push({h0, h0, start_index, 0.0f});

  int expansions = 0;
  bool found = false;
  while (!open.empty()) {
    const OpenEntry current = open.top();
    open.pop();
    const auto ci = static_cast<std::size_t>(current.index);
    if (closed[ci] || current.g > best_g[ci]) continue;
    closed[ci] = 1;
    if (current.index == goal_index) {
      found = true;
      break;
    }
    if (++expansions > options.max_expansions) return {};

    const int cx = current.index % w;
    const int cy = current.index / w;
    for (int n = 0; n < neighbour_count; ++n) {
      const int nx = cx + kDx[n];
      const int ny = cy + kDy[n];
      if (grid.is_blocked(nx, ny)) continue;
      const bool diagonal = n >= 4;
      if (diagonal && (grid.is_blocked(cx + kDx[n], cy) || grid.is_blocked(cx, cy + kDy[n]))) continue;
      const int ni = ny * w + nx;
      const auto nu = static_cast<std::size_t>(ni);
      if (closed[nu]) continue;
      const float g = current.g + (diagonal ? kSqrt2 : 1.0f);
      if (g >= best_g[nu]) continue;
      best_g[nu] = g;
      parent[nu] = current.index;
      const float h = octile(gx - nx, gy - ny, options.allow_diagonal);
      open.push({g + h, h, ni, g});
    }
  }
  if (!found) return {};

  std::vector<Vec2> path;
  for (int index = goal_index; index != start_index; index = parent[static_cast<std::size_t>(index)]) {
    path.push_back(grid.cell_center(index % w, index / w));
  }
  std::reverse(path.begin(), path.end());
  path.back() = goal;
  return path;
}

std::vector<Vec2> simplify_path(const NavGrid&, std::vector<Vec2> path) {
  if (path.size() < 3) return path;
  std::vector<Vec2> out;
  out.push_back(path.front());
  for (std::size_t i = 1; i + 1 < path.size(); ++i) {
    const Vec2 a = out.back();
    const Vec2 b = path[i];
    const Vec2 c = path[i + 1];
    const Vec2 ab{b.x - a.x, b.y - a.y};
    const Vec2 bc{c.x - b.x, c.y - b.y};
    const float cross = ab.x * bc.y - ab.y * bc.x;
    const float dot = ab.x * bc.x + ab.y * bc.y;
    const float tolerance = 1e-4f * std::max(1.0f, length(ab) * length(bc));
    if (std::fabs(cross) <= tolerance && dot > 0.0f) continue;
    out.push_back(b);
  }
  out.push_back(path.back());
  return out;
}

Vec2 seek(Vec2 pos, Vec2 target, float max_speed) noexcept {
  const Vec2 d{target.x - pos.x, target.y - pos.y};
  const float dist = length(d);
  if (dist <= 0.0f) return {};
  return {d.x / dist * max_speed, d.y / dist * max_speed};
}

Vec2 flee(Vec2 pos, Vec2 threat, float max_speed) noexcept { return seek(threat, pos, max_speed); }

Vec2 arrive(Vec2 pos, Vec2 target, float max_speed, float slow_radius) noexcept {
  const Vec2 d{target.x - pos.x, target.y - pos.y};
  const float dist = length(d);
  if (dist <= 0.0f) return {};
  const float speed = slow_radius > 0.0f && dist < slow_radius ? max_speed * (dist / slow_radius) : max_speed;
  return {d.x / dist * speed, d.y / dist * speed};
}

Vec2 separation(Vec2 pos, const std::vector<Vec2>& neighbours, float radius, float max_speed) noexcept {
  if (radius <= 0.0f) return {};
  Vec2 sum;
  for (const Vec2& n : neighbours) {
    const Vec2 d{pos.x - n.x, pos.y - n.y};
    const float dist = length(d);
    if (dist <= 0.0f || dist >= radius) continue;
    const float weight = (1.0f - dist / radius) / dist;
    sum.x += d.x * weight;
    sum.y += d.y * weight;
  }
  const float len = length(sum);
  if (len <= 0.0f) return {};
  const float scale = max_speed * std::min(len, 1.0f) / len;
  return {sum.x * scale, sum.y * scale};
}

SteeringAgent2D::SteeringAgent2D() = default;
SteeringAgent2D::~SteeringAgent2D() = default;

void SteeringAgent2D::set_target_position(Vec2 position) noexcept {
  target_position_ = position;
  has_target_position_ = true;
}

void SteeringAgent2D::invalidate_navigation() noexcept {
  has_grid_ = false;
  grid_node_ = kInvalidNodeId;
  grid_tilemap_ = nullptr;
  path_.clear();
  path_index_ = 0;
  repath_timer_ = 0.0f;
}

bool SteeringAgent2D::resolve_target(Vec2& out) const {
  if (has_target_position_) {
    out = target_position_;
    return true;
  }
  const Scene* scene = this->scene();
  if (!scene || target.empty()) return false;
  const Node* node = scene->find_node_by_name(target);
  if (!node) return false;
  out = node->world_transform().position;
  return true;
}

void SteeringAgent2D::repath(Vec2 position, Vec2 goal) {
  repath_timer_ = repath_interval;
  path_.clear();
  path_index_ = 0;
  const Scene* scene = this->scene();
  if (!scene) return;
  const Node* map_node = nullptr;
  if (tilemap.empty()) {
    for (const auto& root : scene->roots()) {
      if ((map_node = first_tilemap_node(*root))) break;
    }
  } else {
    map_node = scene->find_node_by_name(tilemap);
  }
  const Tilemap* map = map_node ? tilemap_of(*map_node) : nullptr;
  if (!map) {
    has_grid_ = false;
    path_ = {goal};  // no map to plan on: go straight
    return;
  }
  if (!has_grid_ || grid_node_ != map_node->id() || grid_tilemap_ != map) {
    grid_ = NavGrid::from_tilemap(*map_node, *map);
    grid_node_ = map_node->id();
    grid_tilemap_ = map;
    has_grid_ = true;
  }
  PathOptions options;
  options.allow_diagonal = allow_diagonal;
  path_ = find_path(grid_, position, goal, options);
}

void SteeringAgent2D::on_update(float fixed_dt) {
  auto* body = dynamic_cast<RigidBody2D*>(node()->find_component("RigidBody2D"));
  if (!body) return;

  const Vec2 position = node()->world_transform().position;
  Vec2 goal;
  Vec2 desired;
  if (resolve_target(goal)) {
    const Vec2 to_goal{goal.x - position.x, goal.y - position.y};
    const bool flee_mode = behavior == Flee;
    if (flee_mode) {
      desired = flee(position, goal, max_speed);
    } else if (length(to_goal) > stop_distance) {
      const bool arrive_mode = behavior == Arrive;
      Vec2 waypoint = goal;
      if (use_pathfinding) {
        repath_timer_ -= fixed_dt;
        if (repath_timer_ <= 0.0f) repath(position, goal);
        if (!path_.empty()) {
          const float cell = has_grid_ ? std::min(std::fabs(grid_.cell_size.x), std::fabs(grid_.cell_size.y)) : 0.0f;
          const float reach = std::max(stop_distance, cell * 0.25f);
          while (path_index_ + 1 < path_.size()) {
            const Vec2 p = path_[path_index_];
            if (length({p.x - position.x, p.y - position.y}) > reach) break;
            ++path_index_;
          }
          waypoint = path_[path_index_];
        }
      }
      const bool last = !use_pathfinding || path_.empty() || path_index_ + 1 >= path_.size();
      if (!use_pathfinding || !path_.empty()) {
        desired = (last && arrive_mode) ? arrive(position, waypoint, max_speed, slow_radius)
                                        : seek(position, waypoint, max_speed);
      }
    }
  }

  // Move linear_velocity toward the desired velocity, limited by max_acceleration * dt.
  const Vec2 v = body->linear_velocity;
  const Vec2 delta{desired.x - v.x, desired.y - v.y};
  const float dist = length(delta);
  const float limit = max_acceleration * fixed_dt;
  if (dist <= limit || dist == 0.0f) {
    body->linear_velocity = desired;
  } else {
    body->linear_velocity = {v.x + delta.x / dist * limit, v.y + delta.y / dist * limit};
  }
}

void register_navigation_components(ComponentRegistry& registry) {
  using C = SteeringAgent2D;
  registry.register_component(
      "SteeringAgent2D", [] { return std::make_unique<C>(); },
      {
          make_property<C>("target", PropertyType::String, &C::target),
          make_property<C>("behavior", PropertyType::Int, &C::behavior),
          make_property<C>("max_speed", PropertyType::Float, &C::max_speed),
          make_property<C>("max_acceleration", PropertyType::Float, &C::max_acceleration),
          make_property<C>("slow_radius", PropertyType::Float, &C::slow_radius),
          make_property<C>("stop_distance", PropertyType::Float, &C::stop_distance),
          make_property<C>("use_pathfinding", PropertyType::Bool, &C::use_pathfinding),
          make_property<C>("repath_interval", PropertyType::Float, &C::repath_interval),
          make_property<C>("tilemap", PropertyType::String, &C::tilemap),
          make_property<C>("allow_diagonal", PropertyType::Bool, &C::allow_diagonal),
      });
}

}  // namespace arcane
