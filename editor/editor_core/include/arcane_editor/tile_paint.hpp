#pragma once

#include <vector>

#include "arcane/math2d.hpp"
#include "arcane/tilemap.hpp"

namespace arcane::editor {

struct TileEdit {
  int layer = 0, cx = 0, cy = 0, before = 0, after = 0;
};

// Editor tilemap paint tool, independent of ImGui so it can be tested
// headless. The panel feeds it world positions (see viewport_to_world, which
// applies camera pan/zoom) and marks the session dirty whenever a call
// returns true.
//
// Edits made between begin_stroke() and end_stroke() (one mouse drag) form a
// single undo step (begin_stroke is a no-op while one is open); each call returns true only if a cell actually changed.
class TilePaintTool {
 public:
  int layer = 0;
  int tile = 1;  // tile id painted; erasing always writes 0

  void begin_stroke();
  void end_stroke();

  // Paints (or erases) the cell under `world`.
  bool brush(Tilemap& map, const WorldTransform2D& node_world, Vec2 world, bool erase);
  // Fills the rectangle of cells spanned by two world points (inclusive,
  // clamped to the grid; nothing if it lies entirely outside).
  bool fill_rect(Tilemap& map, const WorldTransform2D& node_world, Vec2 a, Vec2 b, bool erase);

  bool can_undo() const noexcept { return !undo_.empty(); }
  bool can_redo() const noexcept { return !redo_.empty(); }
  // Revert / reapply the most recent stroke; true if any cell changed.
  bool undo(Tilemap& map);
  bool redo(Tilemap& map);

 private:
  bool set_cell(Tilemap& map, int cx, int cy, int id);

  using Stroke = std::vector<TileEdit>;
  Stroke current_;
  bool stroke_open_ = false;
  std::vector<Stroke> undo_, redo_;
};

}  // namespace arcane::editor
