// tilemap.* (cells of a Tilemap component in the open scene) and tileset.* (tileset asset files) commands,
// docs/commands.md. Every cell edit is expressed as a list of (layer, x, y, tile) changes, so the inverse of
// paint, erase, fill, terrain paint and CSV import is always one tilemap.set_cells with the previous ids.
// Tileset commands write the tileset file at once, like atlas.* (no persist/dry_run).
#include <algorithm>
#include <filesystem>
#include <sstream>

#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/tilemap.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/tile_paint.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

ToolError fail(const char* code, std::string message, std::string path = {}, std::string hint = {}, std::string file = {}) {
  return ToolError{code, std::move(message), std::move(file), std::move(path), std::move(hint)};
}

struct Cell {
  int layer, x, y, tile;
};

std::expected<Tilemap*, ToolError> resolve_map(ProjectContext& context, const Json& args) {
  auto node = resolve_node(context, args["node"].get<std::string>());
  if (!node) return std::unexpected(node.error());
  auto* map = dynamic_cast<Tilemap*>((*node)->find_component("Tilemap"));
  if (!map) {
    return std::unexpected(fail("COMPONENT_NOT_FOUND", "node " + std::to_string((*node)->id()) + " has no Tilemap",
                                "/node", "Add one with component.add --type Tilemap"));
  }
  return map;
}

std::string node_selector(ProjectContext& context, const Json& args) {
  auto node = resolve_node(context, args["node"].get<std::string>());
  return "id:" + std::to_string((*node)->id());
}

// The tileset file the map points at (assets/**.json whose id matches), if any.
std::optional<Tileset> tileset_of(ProjectContext& context, const Tilemap& map) {
  for (const auto& asset : editor::list_animation_clip_assets(context.session().assets_dir())) {
    if (asset.id != map.tileset) continue;
    auto text = read_text_file(asset.absolute_path);
    if (!text) return std::nullopt;
    auto tileset = load_tileset_from_json(*text);
    if (tileset) return std::move(*tileset);
  }
  return std::nullopt;
}

std::expected<void, ToolError> check_cell(const Tilemap& map, int layer, int x, int y, const std::string& where) {
  if (layer < 0 || layer >= map.layer_count()) {
    return std::unexpected(fail("VALUE_INVALID", "layer " + std::to_string(layer) + " does not exist (map has " +
                                                     std::to_string(map.layer_count()) + ")",
                                where + "layer", "Add one with tilemap.add_layer"));
  }
  if (!map.in_bounds(x, y)) {
    return std::unexpected(fail("VALUE_INVALID", "cell (" + std::to_string(x) + "," + std::to_string(y) +
                                                     ") is outside the " + std::to_string(map.width) + "x" +
                                                     std::to_string(map.height) + " grid",
                                where + "x"));
  }
  return {};
}

std::expected<void, ToolError> check_tile(const std::optional<Tileset>& tileset, int tile, const std::string& path) {
  if (tile < 0) return std::unexpected(fail("VALUE_INVALID", "tile ids are >= 0 (0 = empty)", path));
  if (tileset && tile > tileset->tile_count) {
    return std::unexpected(fail("VALUE_INVALID", "tile " + std::to_string(tile) + " is above the tileset's tile_count " +
                                                     std::to_string(tileset->tile_count),
                                path));
  }
  return {};
}

// Applies validated cells; returns the result with the inverse set_cells (only cells that changed).
CommandResult apply_cells(ProjectContext& context, const Json& args, Tilemap& map, const std::vector<Cell>& cells) {
  Json before = Json::array(), after = Json::array();  // after: the cells as written (the redo)
  for (const Cell& c : cells) {
    const int old = map.tile_at(c.layer, c.x, c.y);
    if (old == c.tile) continue;
    before.push_back({{"layer", c.layer}, {"x", c.x}, {"y", c.y}, {"tile", old}});
    after.push_back({{"layer", c.layer}, {"x", c.x}, {"y", c.y}, {"tile", c.tile}});
    map.set_tile(c.layer, c.x, c.y, c.tile);
  }
  const Json value = {{"changed", after.size()}};
  if (after.empty()) return {value, std::nullopt};
  std::reverse(before.begin(), before.end());  // restore in reverse so repeated cells end at their first value
  const std::string node = node_selector(context, args);
  return {value, Invocation{"tilemap.set_cells", {{"node", node}, {"cells", before}}},
          Invocation{"tilemap.set_cells", {{"node", node}, {"cells", after}}}};
}

// Cells that differ between two copies of one layer.
std::vector<Cell> diff_layer(const Tilemap& map, int layer, const std::vector<int>& before) {
  std::vector<Cell> out;
  for (int y = 0; y < map.height; ++y) {
    for (int x = 0; x < map.width; ++x) {
      const int now = map.tile_at(layer, x, y);
      if (now != before[static_cast<std::size_t>(y * map.width + x)]) out.push_back({layer, x, y, now});
    }
  }
  return out;
}

std::vector<int> copy_layer(const Tilemap& map, int layer) {
  std::vector<int> ids;
  for (int y = 0; y < map.height; ++y) {
    for (int x = 0; x < map.width; ++x) ids.push_back(map.tile_at(layer, x, y));
  }
  return ids;
}

const Json kNode = {{"type", "string"}};
const Json kInt = {{"type", "integer"}};
const Json kLayer = {{"type", "integer"}, {"minimum", 0}};
const Json kTileId = {{"type", "integer"}, {"minimum", 0}};

Json schema(Json properties, std::initializer_list<const char*> required) {
  Json req = Json::array();
  for (const char* r : required) req.push_back(r);
  return {{"type", "object"}, {"properties", std::move(properties)}, {"required", req}, {"additionalProperties", false}};
}

// Parses CSV text: rows of comma-separated non-negative integers (blank lines ignored, CR tolerated).
std::expected<std::vector<std::vector<int>>, ToolError> parse_csv(const std::string& text) {
  std::vector<std::vector<int>> rows;
  std::istringstream lines(text);
  std::string line;
  int row = 0;
  while (std::getline(lines, line)) {
    ++row;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    std::vector<int> ids;
    std::istringstream cells(line);
    std::string cell;
    while (std::getline(cells, cell, ',')) {
      const auto first = cell.find_first_not_of(" \t");
      const auto last = cell.find_last_not_of(" \t");
      const std::string trimmed = first == std::string::npos ? "" : cell.substr(first, last - first + 1);
      if (trimmed.empty() || trimmed.find_first_not_of("0123456789") != std::string::npos || trimmed.size() > 9) {
        return std::unexpected(fail("VALUE_INVALID", "csv line " + std::to_string(row) + ": '" + trimmed +
                                                         "' is not a tile id (non-negative integer)",
                                    "/csv"));
      }
      ids.push_back(std::stoi(trimmed));
    }
    if (line.back() == ',') return std::unexpected(fail("VALUE_INVALID", "csv line " + std::to_string(row) + " ends with a comma", "/csv"));
    if (!rows.empty() && ids.size() != rows.front().size()) {
      return std::unexpected(fail("VALUE_INVALID", "csv line " + std::to_string(row) + " has " + std::to_string(ids.size()) +
                                                       " columns, expected " + std::to_string(rows.front().size()),
                                  "/csv"));
    }
    rows.push_back(std::move(ids));
  }
  if (rows.empty()) return std::unexpected(fail("VALUE_INVALID", "csv has no rows", "/csv"));
  return rows;
}

// ---- tileset files ----------------------------------------------------------------------------------------

struct TilesetFile {
  std::string path;
  Tileset tileset;
};

std::expected<std::string, ToolError> check_tileset_path(const std::string& path) {
  if (!is_project_relative_path(path) || !path.ends_with(".json")) {
    return std::unexpected(fail("ARGS_INVALID", "'" + path + "' is not a project-relative '/' path to a .json file",
                                "/tileset", "e.g. assets/tiles.json"));
  }
  if (!path.starts_with("assets/")) {
    return std::unexpected(fail("ARGS_INVALID", "tilesets live under assets/ (asset ids derive from that path)",
                                "/tileset", "e.g. assets/tiles.json"));
  }
  return path;
}

std::expected<TilesetFile, ToolError> load_tileset_file(ProjectContext& context, const Json& args) {
  auto path = check_tileset_path(args["tileset"].get<std::string>());
  if (!path) return std::unexpected(path.error());
  auto text = read_text_file(context.project_dir() / *path);
  if (!text) return std::unexpected(fail("IO_ERROR", text.error(), "/tileset", "Create it with tileset.create", *path));
  auto tileset = load_tileset_from_json(*text);
  if (!tileset) return std::unexpected(fail("SCHEMA_INVALID", tileset.error(), "", "", *path));
  return TilesetFile{*path, std::move(*tileset)};
}

// The file text before a write (nullopt = the file did not exist), and the tileset.restore call that puts it back.
std::optional<std::string> tileset_text_on_disk(ProjectContext& context, const std::string& path) {
  auto text = read_text_file(context.project_dir() / path);
  if (!text) return std::nullopt;
  return std::move(*text);
}

Invocation tileset_restore_call(const std::string& path, const std::optional<std::string>& previous) {
  Json args = {{"tileset", path}};
  if (previous) args["content"] = *previous;
  else args["absent"] = true;
  return Invocation{"tileset.restore", args};
}

// `texture` is an assets/ image path or an unsigned asset id.
std::expected<AssetId, ToolError> resolve_texture(ProjectContext& context, const Json& texture) {
  if (texture.is_number_unsigned()) return texture.get<AssetId>();
  if (texture.is_string() && is_project_relative_path(texture.get<std::string>()) && texture.get<std::string>().starts_with("assets/")) {
    if (!std::filesystem::exists(context.project_dir() / texture.get<std::string>())) {
      return std::unexpected(fail("IO_ERROR", "texture file " + texture.get<std::string>() + " does not exist", "/texture"));
    }
    return editor::asset_id_for_relative_path(texture.get<std::string>().substr(7));
  }
  return std::unexpected(fail("ARGS_INVALID", "texture is an assets/ image path or an unsigned asset id", "/texture"));
}

std::expected<void, ToolError> write_tileset(ProjectContext& context, const TilesetFile& file) {
  auto text = save_tileset_canonical_json(file.tileset);
  if (!text) return std::unexpected(fail("VALUE_INVALID", text.error(), "", "", file.path));
  const auto target = context.project_dir() / file.path;
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  if (auto ok = write_text_file_atomic(target, *text); !ok) return std::unexpected(fail("IO_ERROR", ok.error(), "", "", file.path));
  return {};
}

Json rule_json(const TerrainRule& rule) { return {{"terrain", rule.terrain}, {"tiles", rule.tiles}}; }

Json tileset_json(const TilesetFile& file) {
  const Tileset& t = file.tileset;
  Json tiles = Json::array(), rules = Json::array();
  for (std::size_t i = 0; i < t.tiles.size(); ++i) {
    tiles.push_back({{"id", i + 1}, {"solid", t.tiles[i].solid}, {"terrain", t.tiles[i].terrain}, {"tags", t.tiles[i].tags}});
  }
  for (const auto& rule : t.terrains) rules.push_back(rule_json(rule));
  return {{"tileset", file.path}, {"texture", t.texture}, {"tile_width", t.tile_width}, {"tile_height", t.tile_height},
          {"columns", t.columns}, {"tile_count", t.tile_count}, {"tiles", tiles}, {"terrains", rules}};
}

// Content problems the format allows to be written but that would paint nonsense.
Json tileset_problems(const Tileset& t) {
  Json problems = Json::array();
  auto add = [&](std::string where, std::string message) { problems.push_back({{"path", where}, {"message", message}}); };
  if (t.tiles.size() > static_cast<std::size_t>(t.tile_count)) add("/tiles", "more tile entries than tile_count");
  std::vector<int> seen;
  for (std::size_t i = 0; i < t.terrains.size(); ++i) {
    const auto& rule = t.terrains[i];
    const std::string base = "/terrains/" + std::to_string(i);
    if (std::find(seen.begin(), seen.end(), rule.terrain) != seen.end()) add(base, "duplicate terrain " + std::to_string(rule.terrain));
    seen.push_back(rule.terrain);
    for (int mask = 0; mask < 16; ++mask) {
      const int id = rule.tiles[static_cast<std::size_t>(mask)];
      if (id < 0 || id > t.tile_count) {
        add(base + "/tiles/" + std::to_string(mask), "tile " + std::to_string(id) + " is outside 0.." + std::to_string(t.tile_count));
      } else if (id != 0 && t.terrain_of(id) != rule.terrain) {
        add(base + "/tiles/" + std::to_string(mask),
            "tile " + std::to_string(id) + " is not marked terrain " + std::to_string(rule.terrain) +
                " (tile terrain " + std::to_string(t.terrain_of(id)) + "): neighbours would not recognise it");
      }
    }
    if (rule.tiles[0] == 0 && std::none_of(rule.tiles.begin(), rule.tiles.end(), [](int id) { return id != 0; })) {
      add(base, "terrain " + std::to_string(rule.terrain) + " has no tiles at all");
    }
  }
  return problems;
}

Json tile_state(const Tileset& t, int tile) {
  const bool has = tile <= static_cast<int>(t.tiles.size());
  return {{"solid", has ? t.tiles[static_cast<std::size_t>(tile - 1)].solid : false},
          {"terrain", has ? t.tiles[static_cast<std::size_t>(tile - 1)].terrain : 0},
          {"tags", has ? t.tiles[static_cast<std::size_t>(tile - 1)].tags : std::vector<std::string>{}}};
}

const Json kTilesetArg = {{"type", "string"}};

}  // namespace

void register_tilemap_commands(CommandRegistry& registry) {
  registry.add(
      {"tilemap.info", "Describe a Tilemap component: size, cell size, layers and the tileset it points at.",
       schema({{"node", kNode}}, {"node"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const auto tileset = tileset_of(context, **map);
        Json layers = Json::array();
        for (int l = 0; l < (*map)->layer_count(); ++l) {
          int used = 0;
          for (int y = 0; y < (*map)->height; ++y)
            for (int x = 0; x < (*map)->width; ++x) used += (*map)->tile_at(l, x, y) != 0;
          layers.push_back({{"layer", l}, {"non_empty_cells", used}});
        }
        return CommandResult{{{"width", (*map)->width}, {"height", (*map)->height}, {"tile_width", (*map)->tile_width},
                              {"tile_height", (*map)->tile_height}, {"tileset", (*map)->tileset},
                              {"tileset_found", tileset.has_value()},
                              {"tile_count", tileset ? Json(tileset->tile_count) : Json(nullptr)}, {"layers", layers}},
                             std::nullopt};
      });

  registry.add(
      {"tilemap.get", "Read one cell (tile id, 0 = empty).",
       schema({{"node", kNode}, {"layer", kLayer}, {"x", kInt}, {"y", kInt}}, {"node", "x", "y"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const int layer = args.value("layer", 0);
        if (auto ok = check_cell(**map, layer, args["x"], args["y"], "/"); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"layer", layer}, {"x", args["x"]}, {"y", args["y"]}, {"tile", (*map)->tile_at(layer, args["x"], args["y"])}},
                             std::nullopt};
      });

  registry.add(
      {"tilemap.add_layer", "Append an empty layer; the result names its index.",
       schema({{"node", kNode}}, {"node"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const std::string before = (*map)->layers_to_string();
        (*map)->add_layer();
        auto* component = *map;
        return CommandResult{{{"layer", (*map)->layer_count() - 1}},
                             Invocation{"prop.set", {{"node", node_selector(context, args)}, {"component", component->id()},
                                                     {"name", "layers"}, {"value", before}}}};
      });

  registry.add(
      {"tilemap.set_cells",
       "Set many cells at once: cells is a list of {layer, x, y, tile}. All are validated first; nothing changes if any is "
       "invalid. This is also the inverse of every other cell edit.",
       schema({{"node", kNode}, {"cells", {{"type", "array"}}}}, {"node", "cells"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const auto tileset = tileset_of(context, **map);
        std::vector<Cell> cells;
        for (std::size_t i = 0; i < args["cells"].size(); ++i) {
          const Json& c = args["cells"][i];
          const std::string where = "/cells/" + std::to_string(i) + "/";
          if (!c.is_object() || !c.contains("x") || !c.contains("y") || !c.contains("tile") || !c["x"].is_number_integer() ||
              !c["y"].is_number_integer() || !c["tile"].is_number_integer() || (c.contains("layer") && !c["layer"].is_number_integer())) {
            return std::unexpected(fail("ARGS_INVALID", "each cell is {layer?, x, y, tile} with integers", where));
          }
          const Cell cell{c.value("layer", 0), c["x"], c["y"], c["tile"]};
          if (auto ok = check_cell(**map, cell.layer, cell.x, cell.y, where); !ok) return std::unexpected(ok.error());
          if (auto ok = check_tile(tileset, cell.tile, where + "tile"); !ok) return std::unexpected(ok.error());
          cells.push_back(cell);
        }
        return apply_cells(context, args, **map, cells);
      });

  registry.add(
      {"tilemap.paint", "Paint one cell with a tile id (layer defaults to 0).",
       schema({{"node", kNode}, {"layer", kLayer}, {"x", kInt}, {"y", kInt}, {"tile", kTileId}}, {"node", "x", "y", "tile"}),
       {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const Cell cell{args.value("layer", 0), args["x"], args["y"], args["tile"]};
        if (auto ok = check_cell(**map, cell.layer, cell.x, cell.y, "/"); !ok) return std::unexpected(ok.error());
        if (auto ok = check_tile(tileset_of(context, **map), cell.tile, "/tile"); !ok) return std::unexpected(ok.error());
        return apply_cells(context, args, **map, {cell});
      });

  registry.add(
      {"tilemap.erase", "Clear one cell (tile 0).",
       schema({{"node", kNode}, {"layer", kLayer}, {"x", kInt}, {"y", kInt}}, {"node", "x", "y"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const Cell cell{args.value("layer", 0), args["x"], args["y"], 0};
        if (auto ok = check_cell(**map, cell.layer, cell.x, cell.y, "/"); !ok) return std::unexpected(ok.error());
        return apply_cells(context, args, **map, {cell});
      });

  registry.add(
      {"tilemap.fill_rect",
       "Fill the inclusive cell rectangle x0,y0..x1,y1 with a tile (0 = erase). Corners may be given in any order and must be inside the grid.",
       schema({{"node", kNode}, {"layer", kLayer}, {"x0", kInt}, {"y0", kInt}, {"x1", kInt}, {"y1", kInt}, {"tile", kTileId}},
              {"node", "x0", "y0", "x1", "y1", "tile"}),
       {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const int layer = args.value("layer", 0), tile = args["tile"];
        const int ax = args["x0"], bx = args["x1"], ay = args["y0"], by = args["y1"];
        const int x0 = std::min(ax, bx), x1 = std::max(ax, bx), y0 = std::min(ay, by), y1 = std::max(ay, by);
        if (auto ok = check_cell(**map, layer, x0, y0, "/"); !ok) return std::unexpected(ok.error());
        if (auto ok = check_cell(**map, layer, x1, y1, "/"); !ok) return std::unexpected(ok.error());
        if (auto ok = check_tile(tileset_of(context, **map), tile, "/tile"); !ok) return std::unexpected(ok.error());
        std::vector<Cell> cells;
        for (int y = y0; y <= y1; ++y)
          for (int x = x0; x <= x1; ++x) cells.push_back({layer, x, y, tile});
        return apply_cells(context, args, **map, cells);
      });

  registry.add(
      {"tilemap.terrain_paint",
       "Paint (or with erase=true clear) a cell with auto-tiling: the tile is picked from the tileset's rule for `terrain` "
       "by its N/E/S/W neighbours, and the neighbours of that terrain are re-picked (the editor's terrain brush).",
       schema({{"node", kNode}, {"layer", kLayer}, {"x", kInt}, {"y", kInt}, {"terrain", {{"type", "integer"}, {"minimum", 1}}},
               {"erase", {{"type", "boolean"}}}},
              {"node", "x", "y", "terrain"}),
       {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const int layer = args.value("layer", 0), x = args["x"], y = args["y"], terrain = args["terrain"];
        if (auto ok = check_cell(**map, layer, x, y, "/"); !ok) return std::unexpected(ok.error());
        const auto tileset = tileset_of(context, **map);
        if (!tileset) {
          return std::unexpected(fail("TILESET_NOT_FOUND", "the map's tileset id " + std::to_string((*map)->tileset) +
                                                              " matches no tileset file under assets/",
                                      "", "Set the Tilemap tileset property"));
        }
        const bool erase = args.value("erase", false);
        const bool has_rule = std::any_of(tileset->terrains.begin(), tileset->terrains.end(),
                                          [&](const TerrainRule& r) { return r.terrain == terrain; });
        if (!erase && !has_rule) {
          return std::unexpected(fail("VALUE_INVALID", "the tileset has no rule for terrain " + std::to_string(terrain), "/terrain",
                                      "Add one with tileset.set_rule"));
        }
        const std::vector<int> before = copy_layer(**map, layer);
        editor::TilePaintTool tool;
        tool.layer = layer;
        tool.terrain_cell(**map, *tileset, x, y, terrain, erase);
        // Report the diff, then replay it through apply_cells so the inverse is built the common way.
        const std::vector<Cell> changed = diff_layer(**map, layer, before);
        for (const Cell& c : changed) {
          (*map)->set_tile(layer, c.x, c.y, before[static_cast<std::size_t>(c.y * (*map)->width + c.x)]);
        }
        return apply_cells(context, args, **map, changed);
      });

  registry.add(
      {"tilemap.dump_csv",
       "Dump one layer as CSV (one row per grid row, tile ids, 0 = empty). With `file` (project-relative), also writes it there at once.",
       schema({{"node", kNode}, {"layer", kLayer}, {"file", {{"type", "string"}}}}, {"node"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        const int layer = args.value("layer", 0);
        if (layer < 0 || layer >= (*map)->layer_count()) {
          return std::unexpected(fail("VALUE_INVALID", "layer " + std::to_string(layer) + " does not exist", "/layer"));
        }
        std::string csv;
        for (int y = 0; y < (*map)->height; ++y) {
          for (int x = 0; x < (*map)->width; ++x) {
            if (x) csv += ',';
            csv += std::to_string((*map)->tile_at(layer, x, y));
          }
          csv += '\n';
        }
        Json value = {{"layer", layer}, {"width", (*map)->width}, {"height", (*map)->height}, {"csv", csv}};
        if (args.contains("file")) {
          const std::string file = args["file"];
          if (!is_project_relative_path(file)) {
            return std::unexpected(fail("ARGS_INVALID", "'" + file + "' is not a project-relative '/' path", "/file"));
          }
          const auto target = context.project_dir() / file;
          std::error_code ec;
          std::filesystem::create_directories(target.parent_path(), ec);
          if (auto ok = write_text_file_atomic(target, csv); !ok) return std::unexpected(fail("IO_ERROR", ok.error(), "", "", file));
          value["file"] = file;
        }
        return CommandResult{std::move(value), std::nullopt};
      });

  registry.add(
      {"tilemap.import_csv",
       "Write CSV tile ids into a layer starting at cell (x, y) (default 0,0). Give the text as `csv` or read it from a project-relative `file`. "
       "Rows must be the same length, fit the grid and use valid tile ids; nothing changes otherwise.",
       schema({{"node", kNode}, {"layer", kLayer}, {"csv", {{"type", "string"}, {"cli_text", true}}}, {"file", {{"type", "string"}}},
               {"x", kInt}, {"y", kInt}},
              {"node"}),
       {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto map = resolve_map(context, args);
        if (!map) return std::unexpected(map.error());
        if (args.contains("csv") == args.contains("file")) {
          return std::unexpected(fail("ARGS_INVALID", "give exactly one of csv or file", "", "Use --csv TEXT or --file PATH"));
        }
        std::string text;
        if (args.contains("csv")) {
          text = args["csv"];
        } else {
          const std::string file = args["file"];
          if (!is_project_relative_path(file)) {
            return std::unexpected(fail("ARGS_INVALID", "'" + file + "' is not a project-relative '/' path", "/file"));
          }
          auto read = read_text_file(context.project_dir() / file);
          if (!read) return std::unexpected(fail("IO_ERROR", read.error(), "/file", "", file));
          text = std::move(*read);
        }
        auto rows = parse_csv(text);
        if (!rows) return std::unexpected(rows.error());
        const int layer = args.value("layer", 0), ox = args.value("x", 0), oy = args.value("y", 0);
        const int cols = static_cast<int>(rows->front().size()), count = static_cast<int>(rows->size());
        if (auto ok = check_cell(**map, layer, ox, oy, "/"); !ok) return std::unexpected(ok.error());
        if (ox + cols > (*map)->width || oy + count > (*map)->height) {
          return std::unexpected(fail("VALUE_INVALID", "a " + std::to_string(cols) + "x" + std::to_string(count) +
                                                           " csv at (" + std::to_string(ox) + "," + std::to_string(oy) +
                                                           ") does not fit the " + std::to_string((*map)->width) + "x" +
                                                           std::to_string((*map)->height) + " grid",
                                      "/csv"));
        }
        const auto tileset = tileset_of(context, **map);
        std::vector<Cell> cells;
        for (int r = 0; r < count; ++r) {
          for (int c = 0; c < cols; ++c) {
            const int tile = (*rows)[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
            if (auto ok = check_tile(tileset, tile, "/csv"); !ok) return std::unexpected(ok.error());
            cells.push_back({layer, ox + c, oy + r, tile});
          }
        }
        return apply_cells(context, args, **map, cells);
      });

  // ---- tileset.* -----------------------------------------------------------------------------------------

  registry.add({"tileset.info", "Show a tileset file: sheet layout, per-tile solid/terrain flags and terrain rules.",
                schema({{"tileset", kTilesetArg}}, {"tileset"}), {{"type", "object"}}, false},
               [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
                 auto file = load_tileset_file(context, args);
                 if (!file) return std::unexpected(file.error());
                 return CommandResult{tileset_json(*file), std::nullopt};
               });

  registry.add({"tileset.validate",
                "Check a tileset's content: rule tiles inside tile_count, rule tiles marked with their terrain, duplicate or empty rules. "
                "ok=false (exit 4) lists the problems; the file is never changed.",
                schema({{"tileset", kTilesetArg}}, {"tileset"}), {{"type", "object"}}, false},
               [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
                 auto file = load_tileset_file(context, args);
                 if (!file) return std::unexpected(file.error());
                 const Json problems = tileset_problems(file->tileset);
                 return CommandResult{{{"ok", problems.empty()}, {"tileset", file->path}, {"problems", problems}}, std::nullopt};
               });

  registry.add(
      {"tileset.create",
       "Create a tileset file. `texture` is an image under assets/ (project-relative path, e.g. assets/tiles.png) or a raw asset id. "
       "Fails if the file exists unless overwrite=true.",
       schema({{"tileset", kTilesetArg}, {"texture", {{"cli_text", true}}}, {"tile_width", {{"type", "integer"}, {"minimum", 1}}},
               {"tile_height", {{"type", "integer"}, {"minimum", 1}}}, {"columns", {{"type", "integer"}, {"minimum", 1}}},
               {"tile_count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4096}}}, {"overwrite", {{"type", "boolean"}}}},
              {"tileset", "texture", "tile_width", "tile_height", "columns", "tile_count"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto path = check_tileset_path(args["tileset"].get<std::string>());
        if (!path) return std::unexpected(path.error());
        TilesetFile file{*path, {}};
        auto texture = resolve_texture(context, args["texture"]);
        if (!texture) return std::unexpected(texture.error());
        file.tileset.texture = *texture;
        file.tileset.tile_width = args["tile_width"];
        file.tileset.tile_height = args["tile_height"];
        file.tileset.columns = args["columns"];
        file.tileset.tile_count = args["tile_count"];
        std::error_code ec;
        if (std::filesystem::exists(context.project_dir() / file.path, ec) && !args.value("overwrite", false)) {
          return std::unexpected(fail("CONFLICT", file.path + " already exists", "/tileset", "Pass overwrite=true to replace it", file.path));
        }
        const auto previous = tileset_text_on_disk(context, file.path);
        if (auto ok = write_tileset(context, file); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"tileset", file.path}, {"texture", file.tileset.texture}}, tileset_restore_call(file.path, previous)};
      });

  registry.add(
      {"tileset.set_tile",
       "Set the solid flag, terrain and/or gameplay `tags` (e.g. hazard, pickup; a-z 0-9 _, replaces the list) of tile `tile` (1-based id). Writes the file at once.",
       schema({{"tileset", kTilesetArg}, {"tile", {{"type", "integer"}, {"minimum", 1}}}, {"solid", {{"type", "boolean"}}},
               {"terrain", {{"type", "integer"}, {"minimum", 0}}},
               {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}}}},
              {"tileset", "tile"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto file = load_tileset_file(context, args);
        if (!file) return std::unexpected(file.error());
        Tileset& t = file->tileset;
        const int tile = args["tile"];
        if (tile > t.tile_count) {
          return std::unexpected(fail("VALUE_INVALID", "tile " + std::to_string(tile) + " is above tile_count " + std::to_string(t.tile_count), "/tile"));
        }
        const Json previous = tile_state(t, tile);
        if (t.tiles.size() < static_cast<std::size_t>(tile)) t.tiles.resize(static_cast<std::size_t>(tile));
        TileInfo& info = t.tiles[static_cast<std::size_t>(tile - 1)];
        if (args.contains("solid")) info.solid = args["solid"];
        if (args.contains("terrain")) info.terrain = args["terrain"];
        if (args.contains("tags")) {
          std::vector<std::string> tags = args["tags"].get<std::vector<std::string>>();
          for (const std::string& tag : tags) {
            if (!is_valid_tile_tag(tag)) return std::unexpected(fail("VALUE_INVALID", "tag '" + tag + "' must be 1-32 characters of a-z 0-9 _", "/tags"));
          }
          std::sort(tags.begin(), tags.end());
          tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
          info.tags = std::move(tags);
        }
        const Json now = tile_state(t, tile);
        if (now == previous) return CommandResult{{{"changed", false}, {"tile", tile}, {"state", now}}, std::nullopt};
        if (auto ok = write_tileset(context, *file); !ok) return std::unexpected(ok.error());
        Json inverse_args = {{"tileset", file->path}, {"tile", tile}, {"solid", previous["solid"]}, {"terrain", previous["terrain"]}, {"tags", previous["tags"]}};
        return CommandResult{{{"changed", true}, {"tile", tile}, {"state", now}, {"previous", previous}},
                             Invocation{"tileset.set_tile", inverse_args}};
      });

  registry.add(
      {"tileset.set_rule",
       "Add or replace the auto-tile rule of a terrain: `tiles` is 16 tile ids indexed by neighbour mask (N=1 E=2 S=4 W=8; 0 = no tile). "
       "Run tileset.validate afterwards to check the tiles are marked with the terrain.",
       schema({{"tileset", kTilesetArg}, {"terrain", {{"type", "integer"}, {"minimum", 1}}}, {"tiles", {{"type", "array"}}}},
              {"tileset", "terrain", "tiles"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto file = load_tileset_file(context, args);
        if (!file) return std::unexpected(file.error());
        Tileset& t = file->tileset;
        if (args["tiles"].size() != 16) {
          return std::unexpected(fail("VALUE_INVALID", "tiles must have exactly 16 entries (neighbour masks 0..15)", "/tiles"));
        }
        TerrainRule rule;
        rule.terrain = args["terrain"];
        for (std::size_t i = 0; i < 16; ++i) {
          const Json& id = args["tiles"][i];
          if (!id.is_number_integer() || id.get<int>() < 0 || id.get<int>() > t.tile_count) {
            return std::unexpected(fail("VALUE_INVALID", "entry must be an integer in 0.." + std::to_string(t.tile_count),
                                        "/tiles/" + std::to_string(i)));
          }
          rule.tiles[i] = id;
        }
        auto existing = std::find_if(t.terrains.begin(), t.terrains.end(), [&](const TerrainRule& r) { return r.terrain == rule.terrain; });
        std::optional<Invocation> inverse;
        if (existing == t.terrains.end()) {
          t.terrains.push_back(rule);
          inverse = Invocation{"tileset.remove_rule", {{"tileset", file->path}, {"terrain", rule.terrain}}};
        } else {
          if (existing->tiles == rule.tiles) return CommandResult{{{"changed", false}}, std::nullopt};
          inverse = Invocation{"tileset.set_rule", {{"tileset", file->path}, {"terrain", rule.terrain}, {"tiles", existing->tiles}}};
          *existing = rule;
        }
        if (auto ok = write_tileset(context, *file); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"changed", true}, {"rule", rule_json(rule)}, {"problems", tileset_problems(t)}}, inverse};
      });

  registry.add(
      {"tileset.remove_rule", "Remove the auto-tile rule of a terrain.",
       schema({{"tileset", kTilesetArg}, {"terrain", {{"type", "integer"}, {"minimum", 1}}}}, {"tileset", "terrain"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto file = load_tileset_file(context, args);
        if (!file) return std::unexpected(file.error());
        Tileset& t = file->tileset;
        auto it = std::find_if(t.terrains.begin(), t.terrains.end(), [&](const TerrainRule& r) { return r.terrain == args["terrain"]; });
        if (it == t.terrains.end()) {
          return std::unexpected(fail("VALUE_INVALID", "the tileset has no rule for terrain " + std::to_string(args["terrain"].get<int>()), "/terrain"));
        }
        const TerrainRule removed = *it;
        t.terrains.erase(it);
        if (auto ok = write_tileset(context, *file); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"removed", rule_json(removed)}},
                             Invocation{"tileset.set_rule", {{"tileset", file->path}, {"terrain", removed.terrain}, {"tiles", removed.tiles}}}};
      });

  registry.add(
      {"tileset.replace",
       "Write a whole tileset in one step (the editor's Save tileset): sheet layout like tileset.create, `tiles` "
       "[{id, solid?, terrain?}] (1-based ids, others default) and `terrains` [{terrain, tiles[16]}]. Creates or overwrites the "
       "file at once; the inverse is tileset.restore.",
       schema({{"tileset", kTilesetArg}, {"texture", {{"cli_text", true}}}, {"tile_width", {{"type", "integer"}, {"minimum", 1}}},
               {"tile_height", {{"type", "integer"}, {"minimum", 1}}}, {"columns", {{"type", "integer"}, {"minimum", 1}}},
               {"tile_count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4096}}}, {"tiles", {{"type", "array"}}},
               {"terrains", {{"type", "array"}}}},
              {"tileset", "texture", "tile_width", "tile_height", "columns", "tile_count"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto path = check_tileset_path(args["tileset"].get<std::string>());
        if (!path) return std::unexpected(path.error());
        auto texture = resolve_texture(context, args["texture"]);
        if (!texture) return std::unexpected(texture.error());
        TilesetFile file{*path, {}};
        Tileset& t = file.tileset;
        t.texture = *texture;
        t.tile_width = args["tile_width"];
        t.tile_height = args["tile_height"];
        t.columns = args["columns"];
        t.tile_count = args["tile_count"];
        t.tiles.resize(static_cast<std::size_t>(t.tile_count));
        if (args.contains("tiles")) {
          for (std::size_t i = 0; i < args["tiles"].size(); ++i) {
            const Json& entry = args["tiles"][i];
            const std::string where = "/tiles/" + std::to_string(i);
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_integer()) {
              return std::unexpected(fail("ARGS_INVALID", "a tile entry is {id, solid?, terrain?}", where));
            }
            const int id = entry["id"];
            if (id < 1 || id > t.tile_count) {
              return std::unexpected(fail("VALUE_INVALID", "tile id must be in 1.." + std::to_string(t.tile_count), where + "/id"));
            }
            TileInfo& info = t.tiles[static_cast<std::size_t>(id - 1)];
            if (entry.contains("solid")) info.solid = entry["solid"].get<bool>();
            if (entry.contains("terrain")) info.terrain = entry["terrain"].get<int>();
          }
        }
        // Trailing default entries are not written (the same bytes as create + set_tile would give).
        while (!t.tiles.empty() && !t.tiles.back().solid && t.tiles.back().terrain == 0) t.tiles.pop_back();
        if (args.contains("terrains")) {
          for (std::size_t i = 0; i < args["terrains"].size(); ++i) {
            const Json& entry = args["terrains"][i];
            const std::string where = "/terrains/" + std::to_string(i);
            if (!entry.is_object() || !entry.contains("terrain") || !entry.contains("tiles") || !entry["tiles"].is_array() ||
                entry["tiles"].size() != 16) {
              return std::unexpected(fail("ARGS_INVALID", "a terrain entry is {terrain, tiles[16]}", where));
            }
            TerrainRule rule;
            rule.terrain = entry["terrain"];
            for (std::size_t m = 0; m < 16; ++m) {
              const Json& id = entry["tiles"][m];
              if (!id.is_number_integer() || id.get<int>() < 0 || id.get<int>() > t.tile_count) {
                return std::unexpected(fail("VALUE_INVALID", "entry must be an integer in 0.." + std::to_string(t.tile_count),
                                            where + "/tiles/" + std::to_string(m)));
              }
              rule.tiles[m] = id;
            }
            t.terrains.push_back(rule);
          }
        }
        const auto previous = tileset_text_on_disk(context, file.path);
        if (auto ok = write_tileset(context, file); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"tileset", file.path}, {"tile_count", t.tile_count}, {"terrains", t.terrains.size()}},
                             tileset_restore_call(file.path, previous)};
      });

  registry.add(
      {"tileset.restore",
       "Put a tileset file back to an earlier state: write `content` (the exact previous file text) or, with absent=true, delete "
       "the file. Used as the inverse of the other tileset.* writes; not meant for authoring.",
       schema({{"tileset", kTilesetArg}, {"content", {{"type", "string"}}}, {"absent", {{"type", "boolean"}}}}, {"tileset"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto path = check_tileset_path(args["tileset"].get<std::string>());
        if (!path) return std::unexpected(path.error());
        const bool absent = args.value("absent", false);
        if (absent == args.contains("content")) {
          return std::unexpected(fail("ARGS_INVALID", "give either content or absent=true", "/content"));
        }
        const auto previous = tileset_text_on_disk(context, *path);
        const auto target = context.project_dir() / *path;
        std::error_code ec;
        if (absent) {
          std::filesystem::remove(target, ec);
          if (ec) return std::unexpected(fail("IO_ERROR", "cannot remove " + *path + ": " + ec.message(), "", "", *path));
        } else {
          std::filesystem::create_directories(target.parent_path(), ec);
          if (auto ok = write_text_file_atomic(target, args["content"].get<std::string>()); !ok) {
            return std::unexpected(fail("IO_ERROR", ok.error(), "", "", *path));
          }
        }
        return CommandResult{{{"tileset", *path}, {"restored", absent ? "absent" : "content"}}, tileset_restore_call(*path, previous)};
      });
}

}  // namespace arcane::tooling
