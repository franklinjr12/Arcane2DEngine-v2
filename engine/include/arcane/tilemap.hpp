#pragma once

#include <expected>
#include <string>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/node.hpp"
#include "arcane/property.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

class ComponentRegistry;

// A tileset is its own JSON asset (format_version 1): a texture laid out as a
// regular grid of equally sized tiles. Tile ids are 1-based (id 0 = empty
// cell), counted left-to-right, top-to-bottom. UVs come from the grid
// (columns x rows) alone, so no texture size is needed to compute them.
struct Tileset {
  AssetId texture = 0;
  int tile_width = 32;   // size of one tile in the texture, in texels
  int tile_height = 32;
  int columns = 1;
  int tile_count = 1;
};

ARCANE_API std::string save_tileset_to_json(const Tileset& tileset);
ARCANE_API std::expected<Tileset, std::string> load_tileset_from_json(const std::string& text);

// UV rect of 1-based `tile_id`; the full texture for an invalid id/tileset.
ARCANE_API UvRect tile_uv(const Tileset& tileset, int tile_id) noexcept;

// Axis-aligned rectangle in the tilemap's local pixel space.
struct TileRect {
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
};

// A grid of tiles in one or more layers. Layout: the node's world position is
// the top-left corner of cell (0, 0); x grows right and y grows down (same as
// the renderer). Only the node's position and scale are honoured, not its
// rotation. Layers are drawn in index order (0 is the back).
//
// Tile data persists as one String property, `layers`: layers separated by
// ';', tile ids within a layer by ',' in row-major order (width*height ids).
// Missing trailing ids read as 0, so width/height may load in any order.
class ARCANE_API Tilemap : public Component {
 public:
  AssetId tileset = 0;
  float tile_width = 32.0f;   // size of one cell in world pixels
  float tile_height = 32.0f;
  int width = 16;             // cells
  int height = 16;
  bool collide = false;       // build static colliders from solid tiles (Play)
  std::string solid_tiles;    // comma-separated tile ids that block movement

  int layer_count() const noexcept { return static_cast<int>(layers_.size()); }
  void add_layer() { layers_.emplace_back(); }

  bool in_bounds(int cx, int cy) const noexcept { return cx >= 0 && cy >= 0 && cx < width && cy < height; }
  // 0 for empty cells and for out-of-range layer/cell.
  int tile_at(int layer, int cx, int cy) const noexcept;
  // False (and no change) if the layer or cell is out of range. Grows the layer storage on demand.
  bool set_tile(int layer, int cx, int cy, int tile_id);

  // Cell containing `local` (pixels relative to the map's top-left). Returns false if outside the grid.
  bool local_to_cell(Vec2 local, int& cx, int& cy) const noexcept;
  // Same for a world position, given the owning node's world transform.
  bool world_to_cell(const WorldTransform2D& node_world, Vec2 world, int& cx, int& cy) const noexcept;
  // Top-left corner of a cell in local pixels.
  Vec2 cell_to_local(int cx, int cy) const noexcept { return {cx * tile_width, cy * tile_height}; }

  bool is_solid(int tile_id) const;
  // Static collision boxes: horizontal runs of solid cells (any layer) merged per row.
  std::vector<TileRect> solid_rects() const;

  std::string layers_to_string() const;
  void set_layers_from_string(const std::string& text);

 private:
  std::vector<std::vector<int>> layers_;
};

ARCANE_API void register_tilemap_components(ComponentRegistry& registry);

}  // namespace arcane
