#include "arcane_editor/tile_paint.hpp"

#include <algorithm>
#include <cmath>

namespace arcane::editor {

namespace {

// Cell coordinates of `world` without bounds checking (may be outside the grid).
bool unclamped_cell(const Tilemap& map, const WorldTransform2D& w, Vec2 world, int& cx, int& cy) {
  if (map.tile_width <= 0.0f || map.tile_height <= 0.0f || w.scale.x == 0.0f || w.scale.y == 0.0f) return false;
  cx = static_cast<int>(std::floor((world.x - w.position.x) / w.scale.x / map.tile_width));
  cy = static_cast<int>(std::floor((world.y - w.position.y) / w.scale.y / map.tile_height));
  return true;
}

}  // namespace

void TilePaintTool::begin_stroke() {
  stroke_open_ = true;  // no-op if already open: a drag calls this every frame
}

void TilePaintTool::end_stroke() {
  if (!current_.empty()) {
    undo_.push_back(std::move(current_));
    redo_.clear();
  }
  current_.clear();
  stroke_open_ = false;
}

bool TilePaintTool::set_cell(Tilemap& map, int cx, int cy, int id) {
  const int before = map.tile_at(layer, cx, cy);
  if (before == id || !map.set_tile(layer, cx, cy, id)) return false;
  // An edit outside begin/end_stroke is its own undo step.
  const bool own_stroke = !stroke_open_;
  current_.push_back({layer, cx, cy, before, id});
  if (own_stroke) end_stroke();
  return true;
}

int TilePaintTool::terrain_mask(const Tilemap& map, const Tileset& tileset, int layer, int cx, int cy, int terrain) {
  if (terrain <= 0) return 0;
  int mask = 0;
  const int dx[] = {0, 1, 0, -1}, dy[] = {-1, 0, 1, 0};
  for (int i = 0; i < 4; ++i)
    if (tileset.terrain_of(map.tile_at(layer, cx + dx[i], cy + dy[i])) == terrain) mask |= 1 << i;
  return mask;
}

bool TilePaintTool::terrain_brush(Tilemap& map, const Tileset& tileset, const WorldTransform2D& world_transform,
                                  Vec2 world, int terrain, bool erase) {
  int cx = 0, cy = 0;
  if (!map.world_to_cell(world_transform, world, cx, cy) || layer < 0 || layer >= map.layer_count()) return false;
  const int replacement = erase ? 0 : tileset.tile_for_mask(terrain, terrain_mask(map, tileset, layer, cx, cy, terrain));
  if (!erase && !replacement) return false;
  const bool own = !stroke_open_;
  if (own) begin_stroke();
  bool changed = set_cell(map, cx, cy, replacement);
  const int dx[] = {0, 1, 0, -1}, dy[] = {-1, 0, 1, 0};
  for (int i = 0; i < 4; ++i) {
    int x = cx + dx[i], y = cy + dy[i];
    int adjacent = tileset.terrain_of(map.tile_at(layer, x, y));
    if (!adjacent) continue;
    int id = tileset.tile_for_mask(adjacent, terrain_mask(map, tileset, layer, x, y, adjacent));
    if (id) changed |= set_cell(map, x, y, id);
  }
  if (own) end_stroke();
  return changed;
}

bool TilePaintTool::brush(Tilemap& map, const WorldTransform2D& node_world, Vec2 world, bool erase) {
  int cx = 0, cy = 0;
  if (!map.world_to_cell(node_world, world, cx, cy)) return false;
  return set_cell(map, cx, cy, erase ? 0 : tile);
}

bool TilePaintTool::fill_rect(Tilemap& map, const WorldTransform2D& node_world, Vec2 a, Vec2 b, bool erase) {
  int ax = 0, ay = 0, bx = 0, by = 0;
  if (!unclamped_cell(map, node_world, a, ax, ay) || !unclamped_cell(map, node_world, b, bx, by)) return false;
  const int x0 = std::max(0, std::min(ax, bx)), x1 = std::min(map.width - 1, std::max(ax, bx));
  const int y0 = std::max(0, std::min(ay, by)), y1 = std::min(map.height - 1, std::max(ay, by));
  const bool own_stroke = !stroke_open_;  // a rectangle is one undo step
  if (own_stroke) begin_stroke();
  bool changed = false;
  for (int cy = y0; cy <= y1; ++cy) {
    for (int cx = x0; cx <= x1; ++cx) changed |= set_cell(map, cx, cy, erase ? 0 : tile);
  }
  if (own_stroke) end_stroke();
  return changed;
}

bool TilePaintTool::undo(Tilemap& map) {
  end_stroke();
  if (undo_.empty()) return false;
  Stroke stroke = std::move(undo_.back());
  undo_.pop_back();
  for (auto it = stroke.rbegin(); it != stroke.rend(); ++it) map.set_tile(it->layer, it->cx, it->cy, it->before);
  redo_.push_back(std::move(stroke));
  return true;
}

bool TilePaintTool::redo(Tilemap& map) {
  end_stroke();
  if (redo_.empty()) return false;
  Stroke stroke = std::move(redo_.back());
  redo_.pop_back();
  for (const TileEdit& e : stroke) map.set_tile(e.layer, e.cx, e.cy, e.after);
  undo_.push_back(std::move(stroke));
  return true;
}

}  // namespace arcane::editor
