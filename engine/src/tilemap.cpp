#include "arcane/tilemap.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include "arcane/component_registry.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {
constexpr const char* kTilesetFormat = "arcane.tileset";

std::vector<int> parse_int_list(const std::string& text) {
  std::vector<int> out;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, ',')) {
    try {
      out.push_back(std::stoi(item));
    } catch (const std::exception&) {
      out.push_back(0);
    }
  }
  return out;
}
}  // namespace

std::expected<std::string, std::string> save_tileset_canonical_json(const Tileset& tileset) {
  nlohmann::json j;
  j["format"] = kTilesetFormat;
  j["version"] = 2;
  j["texture"] = tileset.texture;
  j["tile_width"] = tileset.tile_width;
  j["tile_height"] = tileset.tile_height;
  j["columns"] = tileset.columns;
  j["tile_count"] = tileset.tile_count;
  j["tiles"] = nlohmann::json::array();
  for (const auto& tile : tileset.tiles) j["tiles"].push_back({{"solid", tile.solid}, {"terrain", tile.terrain}});
  j["terrains"] = nlohmann::json::array();
  for (const auto& rule : tileset.terrains) j["terrains"].push_back({{"terrain", rule.terrain}, {"tiles", rule.tiles}});
  return canonical::dump(j);
}

std::string save_tileset_to_json(const Tileset& tileset) {
  return save_tileset_canonical_json(tileset).value_or(std::string());
}

namespace {

std::expected<Tileset, std::string> load_tileset(const std::string& text, bool strict_unknown) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("tileset: {}", parsed.error()));
  try {
    const nlohmann::json& j = *parsed;
    canonical::require_object(j, "tileset");
    const canonical::Dialect dialect = canonical::detect_dialect(j, kTilesetFormat, "tileset");
    const bool strict = strict_unknown || dialect == canonical::Dialect::V2;
    if (strict) {
      if (dialect == canonical::Dialect::V2) {
        canonical::require_only_keys(
            j, {"format", "version", "texture", "tile_width", "tile_height", "columns", "tile_count", "tiles", "terrains"},
            "tileset");
      } else {
        canonical::require_only_keys(
            j, {"format_version", "texture", "tile_width", "tile_height", "columns", "tile_count", "tiles", "terrains"},
            "tileset");
      }
    }
    for (const char* key : {"texture", "tile_width", "tile_height", "columns", "tile_count"}) {
      if (!j.contains(key)) return std::unexpected(std::format("tileset: missing '{}'", key));
    }
    Tileset t;
    t.texture = canonical::as_uint(j["texture"], "tileset 'texture'");
    t.tile_width = canonical::as_int(j["tile_width"], "tileset 'tile_width'");
    t.tile_height = canonical::as_int(j["tile_height"], "tileset 'tile_height'");
    t.columns = canonical::as_int(j["columns"], "tileset 'columns'");
    t.tile_count = canonical::as_int(j["tile_count"], "tileset 'tile_count'");
    if (t.tile_width <= 0 || t.tile_height <= 0 || t.columns <= 0 || t.tile_count <= 0) {
      return std::unexpected("tileset: tile size, columns and tile_count must be positive");
    }
    if (j.contains("tiles")) {
      if (!j["tiles"].is_array()) return std::unexpected("tileset: 'tiles' must be an array");
      if (strict && j["tiles"].size() > static_cast<std::size_t>(t.tile_count)) {
        return std::unexpected("tileset: more 'tiles' entries than tile_count");
      }
      for (std::size_t i = 0; i < j["tiles"].size(); ++i) {
        const auto& item = j["tiles"][i];
        const std::string where = std::format("tileset tile {}", i + 1);
        canonical::require_object(item, where);
        if (strict) canonical::require_only_keys(item, {"solid", "terrain"}, where);
        TileInfo info;
        if (item.contains("solid")) info.solid = canonical::as_bool(item["solid"], where + " 'solid'");
        if (item.contains("terrain")) info.terrain = canonical::as_int(item["terrain"], where + " 'terrain'");
        if (strict && info.terrain < 0) return std::unexpected(std::format("tileset: {} has a negative terrain", where));
        t.tiles.push_back(info);
      }
    }
    if (j.contains("terrains")) {
      if (!j["terrains"].is_array()) return std::unexpected("tileset: 'terrains' must be an array");
      for (std::size_t i = 0; i < j["terrains"].size(); ++i) {
        const auto& item = j["terrains"][i];
        const std::string where = std::format("tileset terrain rule {}", i);
        canonical::require_object(item, where);
        if (strict) canonical::require_only_keys(item, {"terrain", "tiles"}, where);
        if (!item.contains("terrain") || !item.contains("tiles")) {
          return std::unexpected(std::format("tileset: {} needs 'terrain' and 'tiles'", where));
        }
        TerrainRule rule;
        rule.terrain = canonical::as_int(item["terrain"], where + " 'terrain'");
        if (!item["tiles"].is_array() || item["tiles"].size() != 16) {
          return std::unexpected(std::format("tileset: {} 'tiles' must be an array of 16 tile ids", where));
        }
        for (std::size_t m = 0; m < 16; ++m) {
          rule.tiles[m] = canonical::as_int(item["tiles"][m], where + " 'tiles'");
          if (strict && (rule.tiles[m] < 0 || rule.tiles[m] > t.tile_count)) {
            return std::unexpected(std::format("tileset: {} references tile {} outside 0..{}", where, rule.tiles[m],
                                                t.tile_count));
          }
        }
        if (strict && rule.terrain <= 0) {
          return std::unexpected(std::format("tileset: {} needs a positive terrain id", where));
        }
        t.terrains.push_back(rule);
      }
    }
    return t;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::format("tileset: {}", ex.what()));
  }
}

}  // namespace

std::expected<Tileset, std::string> load_tileset_from_json(const std::string& text) { return load_tileset(text, false); }

std::expected<Tileset, std::string> load_tileset_from_json_strict(const std::string& text) {
  return load_tileset(text, true);
}

std::expected<std::string, std::string> migrate_tileset_json(const std::string& text) {
  auto tileset = load_tileset_from_json_strict(text);
  if (!tileset) return std::unexpected(tileset.error());
  auto migrated = save_tileset_canonical_json(*tileset);
  if (!migrated) return std::unexpected(migrated.error());
  auto reloaded = load_tileset_from_json_strict(*migrated);
  if (!reloaded) return std::unexpected("migrated tileset does not reload: " + reloaded.error());
  auto again = save_tileset_canonical_json(*reloaded);
  if (!again || *again != *migrated) return std::unexpected(std::string("migrated tileset is not idempotent"));
  return migrated;
}

int Tileset::terrain_of(int tile_id) const noexcept {
  if (tile_id <= 0 || tile_id > tile_count || static_cast<std::size_t>(tile_id) > tiles.size()) return 0;
  return tiles[static_cast<std::size_t>(tile_id - 1)].terrain;
}

int Tileset::tile_for_mask(int terrain, int mask) const noexcept {
  if (terrain <= 0 || mask < 0 || mask >= 16) return 0;
  for (const auto& rule : terrains) if (rule.terrain == terrain) {
    const int id = rule.tiles[static_cast<std::size_t>(mask)];
    return id > 0 && id <= tile_count && terrain_of(id) == terrain ? id : 0;
  }
  return 0;
}

UvRect tile_uv(const Tileset& tileset, int tile_id) noexcept {
  if (tile_id < 1 || tile_id > tileset.tile_count || tileset.columns <= 0) return UvRect{};
  const int index = tile_id - 1;
  const int rows = (tileset.tile_count + tileset.columns - 1) / tileset.columns;
  const int col = index % tileset.columns;
  const int row = index / tileset.columns;
  const float cw = 1.0f / static_cast<float>(tileset.columns);
  const float ch = 1.0f / static_cast<float>(rows);
  return UvRect{static_cast<float>(col) * cw, static_cast<float>(row) * ch, static_cast<float>(col + 1) * cw,
                static_cast<float>(row + 1) * ch};
}

int Tilemap::tile_at(int layer, int cx, int cy) const noexcept {
  if (layer < 0 || layer >= layer_count() || !in_bounds(cx, cy)) return 0;
  const auto& data = layers_[static_cast<std::size_t>(layer)];
  const auto index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx);
  return index < data.size() ? data[index] : 0;
}

bool Tilemap::set_tile(int layer, int cx, int cy, int tile_id) {
  if (layer < 0 || layer >= layer_count() || !in_bounds(cx, cy)) return false;
  auto& data = layers_[static_cast<std::size_t>(layer)];
  const auto index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx);
  if (index >= data.size()) data.resize(index + 1, 0);
  data[index] = std::max(tile_id, 0);
  return true;
}

bool Tilemap::local_to_cell(Vec2 local, int& cx, int& cy) const noexcept {
  if (tile_width <= 0.0f || tile_height <= 0.0f) return false;
  cx = static_cast<int>(std::floor(local.x / tile_width));
  cy = static_cast<int>(std::floor(local.y / tile_height));
  return in_bounds(cx, cy);
}

bool Tilemap::world_to_cell(const WorldTransform2D& node_world, Vec2 world, int& cx, int& cy) const noexcept {
  if (node_world.scale.x == 0.0f || node_world.scale.y == 0.0f) return false;
  return local_to_cell({(world.x - node_world.position.x) / node_world.scale.x,
                         (world.y - node_world.position.y) / node_world.scale.y},
                        cx, cy);
}

bool Tilemap::is_solid(int tile_id) const {
  if (tile_id <= 0) return false;
  const auto ids = parse_int_list(solid_tiles);
  return std::find(ids.begin(), ids.end(), tile_id) != ids.end();
}

std::vector<TileRect> Tilemap::solid_rects() const {
  std::vector<int> solid = parse_int_list(solid_tiles);
  auto cell_solid = [&](int cx, int cy) {
    for (int layer = 0; layer < layer_count(); ++layer) {
      const int id = tile_at(layer, cx, cy);
      if (id > 0 && std::find(solid.begin(), solid.end(), id) != solid.end()) return true;
    }
    return false;
  };

  std::vector<TileRect> rects;
  for (int cy = 0; cy < height; ++cy) {
    int cx = 0;
    while (cx < width) {
      if (!cell_solid(cx, cy)) {
        ++cx;
        continue;
      }
      const int start = cx;
      while (cx < width && cell_solid(cx, cy)) ++cx;
      rects.push_back(TileRect{static_cast<float>(start) * tile_width, static_cast<float>(cy) * tile_height,
                              static_cast<float>(cx - start) * tile_width, tile_height});
    }
  }
  return rects;
}

std::string Tilemap::layers_to_string() const {
  std::string out;
  for (std::size_t l = 0; l < layers_.size(); ++l) {
    if (l > 0) out += ';';
    for (std::size_t i = 0; i < layers_[l].size(); ++i) {
      if (i > 0) out += ',';
      out += std::to_string(layers_[l][i]);
    }
  }
  return out;
}

void Tilemap::set_layers_from_string(const std::string& text) {
  layers_.clear();
  if (text.empty()) return;
  std::stringstream ss(text);
  std::string layer_text;
  while (std::getline(ss, layer_text, ';')) layers_.push_back(parse_int_list(layer_text));
  if (text.back() == ';') layers_.emplace_back();
}

void register_tilemap_components(ComponentRegistry& registry) {
  PropertyDescriptor layers{
      "layers", PropertyType::String,
      [](const Component& c) -> PropertyValue { return static_cast<const Tilemap&>(c).layers_to_string(); },
      [](Component& c, const PropertyValue& v) {
        static_cast<Tilemap&>(c).set_layers_from_string(std::get<std::string>(v));
      },
  };
  registry.register_component(
      "Tilemap", [] { return std::make_unique<Tilemap>(); },
      {
          make_property<Tilemap>("tileset", PropertyType::AssetId, &Tilemap::tileset),
          make_property<Tilemap>("tile_width", PropertyType::Float, &Tilemap::tile_width),
          make_property<Tilemap>("tile_height", PropertyType::Float, &Tilemap::tile_height),
          make_property<Tilemap>("width", PropertyType::Int, &Tilemap::width),
          make_property<Tilemap>("height", PropertyType::Int, &Tilemap::height),
          make_property<Tilemap>("collide", PropertyType::Bool, &Tilemap::collide),
          make_property<Tilemap>("solid_tiles", PropertyType::String, &Tilemap::solid_tiles),
          std::move(layers),
      });
}

}  // namespace arcane
