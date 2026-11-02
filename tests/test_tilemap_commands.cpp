// tilemap.* and tileset.* commands: paint/erase/fill/terrain with exact inverses, CSV round trip and rejection,
// tileset rule authoring and validation, stroke-level undo/redo through EditSession, and the topdown sample's
// existing tilemap.
#include <filesystem>
#include <fstream>
#include <sstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "arcane/builtin_components.hpp"
#include "arcane/game_context.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "arcane_tooling/edit_session.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
using namespace arcane::tooling;
namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct TempProject {
  fs::path dir;
  explicit TempProject(const char* name) : dir(fs::temp_directory_path() / name) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    ComponentRegistry registry;
    register_builtin_components(registry);
    auto session = editor::ProjectSession::create(dir, "Tiles", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    ARCANE_ASSERT_TRUE(session->save(registry).has_value());
    std::vector<unsigned char> px(64 * 16 * 4, 255);
    fs::create_directories(dir / "assets");
    ARCANE_ASSERT_TRUE(stbi_write_png((dir / "assets/tiles.png").string().c_str(), 64, 16, 4, px.data(), 64 * 4) != 0);
  }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

CommandResult ok(CommandRegistry& r, ProjectContext& c, const char* name, Json args, ExecuteOptions o = {}) {
  auto done = r.execute(c, {name, std::move(args)}, o);
  if (!done) std::fprintf(stderr, "%s: %s (%s)\n", name, done.error().message.c_str(), done.error().code.c_str());
  ARCANE_ASSERT_TRUE(done.has_value());
  return *done;
}

std::string code(CommandRegistry& r, ProjectContext& c, const char* name, Json args) {
  auto done = r.execute(c, {name, std::move(args)});
  return done ? "" : done.error().code;
}

std::string dump(CommandRegistry& r, ProjectContext& c, int layer = 0, const char* node = "path:/Map") {
  return ok(r, c, "tilemap.dump_csv", {{"node", node}, {"layer", layer}}).value["csv"];
}

void test_cells_and_inverses() {
  TempProject project("arcane_tilemap_cmds");
  CommandRegistry registry;
  register_all_commands(registry);
  auto opened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(opened.has_value());
  ProjectContext& ctx = **opened;

  ok(registry, ctx, "node.add", {{"name", "Map"}});
  ok(registry, ctx, "component.add", {{"node", "path:/Map"}, {"type", "Tilemap"}});
  ok(registry, ctx, "prop.set", {{"node", "path:/Map"}, {"component", "Tilemap"}, {"name", "width"}, {"value", 4}});
  ok(registry, ctx, "prop.set", {{"node", "path:/Map"}, {"component", "Tilemap"}, {"name", "height"}, {"value", 3}});
  // Tileset: 8 tiles in the 64x16 sheet; ids come from the file name under assets/.
  ok(registry, ctx, "tileset.create", {{"tileset", "assets/set.json"}, {"texture", "assets/tiles.png"}, {"tile_width", 8},
                                       {"tile_height", 16}, {"columns", 8}, {"tile_count", 8}});
  const AssetId set_id = editor::asset_id_for_relative_path("set.json");
  ok(registry, ctx, "prop.set", {{"node", "path:/Map"}, {"component", "Tilemap"}, {"name", "tileset"}, {"value", set_id}});
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.paint", {{"node", "path:/Map"}, {"x", 0}, {"y", 0}, {"tile", 1}}) == "VALUE_INVALID");  // no layer yet
  ARCANE_ASSERT_TRUE(ok(registry, ctx, "tilemap.add_layer", {{"node", "path:/Map"}}).value["layer"] == 0);

  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,0,0,0\n0,0,0,0\n0,0,0,0\n");
  auto painted = ok(registry, ctx, "tilemap.paint", {{"node", "path:/Map"}, {"x", 1}, {"y", 2}, {"tile", 5}});
  ARCANE_ASSERT_TRUE(painted.inverse.has_value());
  ARCANE_ASSERT_TRUE(ok(registry, ctx, "tilemap.get", {{"node", "path:/Map"}, {"x", 1}, {"y", 2}}).value["tile"] == 5);
  ARCANE_ASSERT_TRUE(!ok(registry, ctx, "tilemap.paint", {{"node", "path:/Map"}, {"x", 1}, {"y", 2}, {"tile", 5}}).inverse);  // idempotent
  auto filled = ok(registry, ctx, "tilemap.fill_rect",
                   {{"node", "path:/Map"}, {"x0", 3}, {"y0", 1}, {"x1", 1}, {"y1", 0}, {"tile", 2}});
  ARCANE_ASSERT_TRUE(filled.value["changed"] == 6);
  const std::string after_fill = dump(registry, ctx);
  ARCANE_ASSERT_TRUE(after_fill == "0,2,2,2\n0,2,2,2\n0,5,0,0\n");
  ok(registry, ctx, "tilemap.erase", {{"node", "path:/Map"}, {"x", 1}, {"y", 0}});
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,0,2,2\n0,2,2,2\n0,5,0,0\n");
  // Inverse of the fill restores the cell before it, after the erase is undone first.
  ok(registry, ctx, "tilemap.paint", {{"node", "path:/Map"}, {"x", 1}, {"y", 0}, {"tile", 2}});
  ok(registry, ctx, filled.inverse->command.c_str(), filled.inverse->args);
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,0,0,0\n0,0,0,0\n0,5,0,0\n");

  // Rejections change nothing.
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.paint", {{"node", "path:/Map"}, {"x", 4}, {"y", 0}, {"tile", 1}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.paint", {{"node", "path:/Map"}, {"x", 0}, {"y", 0}, {"tile", 9}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.paint", {{"node", "path:/Map"}, {"x", 0}, {"y", 0}, {"tile", -1}}) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.set_cells",
                          {{"node", "path:/Map"}, {"cells", Json::array({Json{{"x", 0}, {"y", 0}, {"tile", 1}}, Json{{"x", 0}, {"y", 9}, {"tile", 1}}})}}) ==
                     "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,0,0,0\n0,0,0,0\n0,5,0,0\n");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.info", {{"node", "path:/Nope"}}) == "NODE_NOT_FOUND");

  // CSV: dump -> import into another layer round-trips; bad CSV is rejected whole.
  ok(registry, ctx, "tilemap.add_layer", {{"node", "path:/Map"}});
  ok(registry, ctx, "tilemap.fill_rect", {{"node", "path:/Map"}, {"x0", 0}, {"y0", 0}, {"x1", 3}, {"y1", 0}, {"tile", 3}});
  const std::string csv = dump(registry, ctx);
  auto imported = ok(registry, ctx, "tilemap.import_csv", {{"node", "path:/Map"}, {"layer", 1}, {"csv", csv}});
  ARCANE_ASSERT_TRUE(dump(registry, ctx, 1) == csv);
  ARCANE_ASSERT_TRUE(imported.inverse.has_value());
  ok(registry, ctx, imported.inverse->command.c_str(), imported.inverse->args);
  ARCANE_ASSERT_TRUE(dump(registry, ctx, 1) == "0,0,0,0\n0,0,0,0\n0,0,0,0\n");
  for (const char* bad : {"1,2\n3\n", "1,x\n", "9,9,9,9,9\n", "1,2,3,4\n1,2,3,4\n1,2,3,4\n1,2,3,4\n", "1,2,\n", "", "1,2,3,99\n"}) {
    ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.import_csv", {{"node", "path:/Map"}, {"layer", 1}, {"csv", bad}}) != "");
  }
  ARCANE_ASSERT_TRUE(dump(registry, ctx, 1) == "0,0,0,0\n0,0,0,0\n0,0,0,0\n");
  ok(registry, ctx, "tilemap.import_csv", {{"node", "path:/Map"}, {"layer", 1}, {"csv", "7,7\n7,7\n"}, {"x", 2}, {"y", 1}});
  ARCANE_ASSERT_TRUE(dump(registry, ctx, 1) == "0,0,0,0\n0,0,7,7\n0,0,7,7\n");
  ok(registry, ctx, "tilemap.dump_csv", {{"node", "path:/Map"}, {"layer", 1}, {"file", "maps/l1.csv"}});
  ARCANE_ASSERT_TRUE(slurp(project.dir / "maps/l1.csv") == "0,0,0,0\n0,0,7,7\n0,0,7,7\n");
  ok(registry, ctx, "tilemap.import_csv", {{"node", "path:/Map"}, {"layer", 0}, {"file", "maps/l1.csv"}});
  ARCANE_ASSERT_TRUE(dump(registry, ctx, 0) == "0,0,0,0\n0,0,7,7\n0,0,7,7\n");

  // Persisted scene reloads with the same cells.
  ok(registry, ctx, "tilemap.erase", {{"node", "path:/Map"}, {"x", 2}, {"y", 1}}, {.persist = true});
  const std::string saved = dump(registry, ctx);
  auto again = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(again.has_value());
  ARCANE_ASSERT_TRUE(dump(registry, **again) == saved);
}

void test_terrain_and_tileset_rules() {
  TempProject project("arcane_tileset_cmds");
  CommandRegistry registry;
  register_all_commands(registry);
  auto opened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(opened.has_value());
  ProjectContext& ctx = **opened;
  ok(registry, ctx, "tileset.create", {{"tileset", "assets/set.json"}, {"texture", "assets/tiles.png"}, {"tile_width", 8},
                                       {"tile_height", 16}, {"columns", 8}, {"tile_count", 8}});
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tileset.create", {{"tileset", "assets/set.json"}, {"texture", "assets/tiles.png"},
                                                            {"tile_width", 8}, {"tile_height", 16}, {"columns", 8}, {"tile_count", 8}}) == "CONFLICT");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tileset.create", {{"tileset", "../set.json"}, {"texture", 5}, {"tile_width", 8},
                                                            {"tile_height", 16}, {"columns", 8}, {"tile_count", 8}}) == "ARGS_INVALID");
  // Tiles 1 (mask 0 = isolated) and 2 (any neighbour) belong to terrain 1; tile 3 is solid.
  auto marked = ok(registry, ctx, "tileset.set_tile", {{"tileset", "assets/set.json"}, {"tile", 1}, {"terrain", 1}, {"solid", true}});
  ARCANE_ASSERT_TRUE(marked.inverse.has_value());
  ok(registry, ctx, "tileset.set_tile", {{"tileset", "assets/set.json"}, {"tile", 2}, {"terrain", 1}});
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tileset.set_tile", {{"tileset", "assets/set.json"}, {"tile", 9}, {"solid", true}}) == "VALUE_INVALID");
  Json masks = Json::array();
  for (int mask = 0; mask < 16; ++mask) masks.push_back(mask == 0 ? 1 : 2);
  auto rule = ok(registry, ctx, "tileset.set_rule", {{"tileset", "assets/set.json"}, {"terrain", 1}, {"tiles", masks}});
  ARCANE_ASSERT_TRUE(rule.value["problems"].empty());
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tileset.set_rule", {{"tileset", "assets/set.json"}, {"terrain", 1}, {"tiles", Json::array({1, 2})}}) == "VALUE_INVALID");
  Json wrong = masks;
  wrong[3] = 4;  // tile 4 is not marked terrain 1
  ok(registry, ctx, "tileset.set_rule", {{"tileset", "assets/set.json"}, {"terrain", 2}, {"tiles", wrong}});
  auto checked = ok(registry, ctx, "tileset.validate", {{"tileset", "assets/set.json"}});
  ARCANE_ASSERT_TRUE(checked.value["ok"] == false && checked.value["problems"].size() >= 1);
  ok(registry, ctx, "tileset.remove_rule", {{"tileset", "assets/set.json"}, {"terrain", 2}});
  ARCANE_ASSERT_TRUE(ok(registry, ctx, "tileset.validate", {{"tileset", "assets/set.json"}}).value["ok"] == true);
  const std::string file = slurp(project.dir / "assets/set.json");
  ARCANE_ASSERT_TRUE(file.find("\"solid\": true") != std::string::npos);
  auto info = ok(registry, ctx, "tileset.info", {{"tileset", "assets/set.json"}});
  ARCANE_ASSERT_TRUE(info.value["terrains"].size() == 1 && info.value["tiles"][0]["solid"] == true);

  ok(registry, ctx, "node.add", {{"name", "Map"}});
  ok(registry, ctx, "component.add", {{"node", "path:/Map"}, {"type", "Tilemap"}});
  ok(registry, ctx, "prop.set", {{"node", "path:/Map"}, {"component", "Tilemap"}, {"name", "width"}, {"value", 5}});
  ok(registry, ctx, "prop.set", {{"node", "path:/Map"}, {"component", "Tilemap"}, {"name", "height"}, {"value", 1}});
  ok(registry, ctx, "prop.set", {{"node", "path:/Map"}, {"component", "Tilemap"}, {"name", "tileset"},
                                 {"value", editor::asset_id_for_relative_path("set.json")}});
  ok(registry, ctx, "tilemap.add_layer", {{"node", "path:/Map"}});
  auto tp = [&](int x, bool erase = false) {
    return ok(registry, ctx, "tilemap.terrain_paint", {{"node", "path:/Map"}, {"x", x}, {"y", 0}, {"terrain", 1}, {"erase", erase}});
  };
  tp(1);
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,1,0,0,0\n");
  auto second = tp(2);  // neighbour 1 becomes "has east neighbour"... mask E for (1), W for (2)
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,2,2,0,0\n");
  ok(registry, ctx, second.inverse->command.c_str(), second.inverse->args);
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,1,0,0,0\n");
  tp(2);
  tp(1, true);  // erase re-picks the neighbour
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == "0,0,1,0,0\n");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tilemap.terrain_paint", {{"node", "path:/Map"}, {"x", 0}, {"y", 0}, {"terrain", 7}}) == "VALUE_INVALID");

  // Gameplay tags: authored with tileset.set_tile (undoable), read back through GameContext::tile_at.
  auto tagged = ok(registry, ctx, "tileset.set_tile", {{"tileset", "assets/set.json"}, {"tile", 1}, {"tags", Json::array({"pickup", "hazard", "hazard"})}});
  ARCANE_ASSERT_TRUE(tagged.value["state"]["tags"] == Json::array({"hazard", "pickup"}));
  ARCANE_ASSERT_TRUE(code(registry, ctx, "tileset.set_tile", {{"tileset", "assets/set.json"}, {"tile", 1}, {"tags", Json::array({"Bad Tag"})}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(slurp(project.dir / "assets/set.json").find("\"tags\"") != std::string::npos);
  ARCANE_ASSERT_TRUE(ok(registry, ctx, "tileset.validate", {{"tileset", "assets/set.json"}}).value["ok"] == true);
  GameContext game(ctx.project_dir(), ctx.registry());
  auto hit = game.tile_at(ctx.session().scene(), {2 * 32 + 5.0f, 5.0f});  // painted cell (2, 0) holds tile 1
  ARCANE_ASSERT_TRUE(hit.has_value() && hit->tile_id == 1 && hit->cx == 2 && hit->cy == 0);
  ARCANE_ASSERT_TRUE(hit->has_tag("hazard") && hit->has_tag("pickup") && hit->solid);
  ARCANE_ASSERT_TRUE(game.tagged_tile_at(ctx.session().scene(), {2 * 32 + 5.0f, 5.0f}, "hazard").has_value());
  ARCANE_ASSERT_TRUE(!game.tagged_tile_at(ctx.session().scene(), {2 * 32 + 5.0f, 5.0f}, "water").has_value());
  ARCANE_ASSERT_TRUE(!game.tile_at(ctx.session().scene(), {5.0f, 5.0f}).has_value());     // empty cell
  ARCANE_ASSERT_TRUE(!game.tile_at(ctx.session().scene(), {-50.0f, 5.0f}).has_value());   // outside the grid
  ok(registry, ctx, tagged.inverse->command.c_str(), tagged.inverse->args);
  game = GameContext(ctx.project_dir(), ctx.registry());
  ARCANE_ASSERT_TRUE(game.tile_at(ctx.session().scene(), {2 * 32 + 5.0f, 5.0f})->tags.empty());
}

void test_stroke_undo_redo() {
  TempProject project("arcane_tilemap_edit");
  CommandRegistry registry;
  register_all_commands(registry);
  auto opened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(opened.has_value());
  ProjectContext& ctx = **opened;
  EditSession edits(ctx, registry);
  ARCANE_ASSERT_TRUE(edits.run({"node.add", {{"name", "Map"}}}).has_value());
  ARCANE_ASSERT_TRUE(edits.run({"component.add", {{"node", "path:/Map"}, {"type", "Tilemap"}}}).has_value());
  ARCANE_ASSERT_TRUE(edits.run({"tilemap.add_layer", {{"node", "path:/Map"}}}).has_value());
  const std::string empty = dump(registry, ctx);
  // One mouse drag = three paint calls = one undo step.
  for (int x = 0; x < 3; ++x) {
    ARCANE_ASSERT_TRUE(edits.run({"tilemap.paint", {{"node", "path:/Map"}, {"x", x}, {"y", 0}, {"tile", 1}}}, "tile-stroke").has_value());
  }
  edits.end_gesture();
  ARCANE_ASSERT_TRUE(edits.run({"tilemap.paint", {{"node", "path:/Map"}, {"x", 0}, {"y", 1}, {"tile", 1}}}, "tile-stroke").has_value());
  edits.end_gesture();
  const std::string two = dump(registry, ctx);
  ARCANE_ASSERT_TRUE(edits.undo().has_value());
  ARCANE_ASSERT_TRUE(ok(registry, ctx, "tilemap.get", {{"node", "path:/Map"}, {"x", 0}, {"y", 1}}).value["tile"] == 0);
  ARCANE_ASSERT_TRUE(ok(registry, ctx, "tilemap.get", {{"node", "path:/Map"}, {"x", 2}, {"y", 0}}).value["tile"] == 1);
  ARCANE_ASSERT_TRUE(edits.undo().has_value());
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == empty);
  ARCANE_ASSERT_TRUE(edits.redo().has_value() && edits.redo().has_value());
  ARCANE_ASSERT_TRUE(dump(registry, ctx) == two);
  ARCANE_ASSERT_TRUE(edits.modified());
}

void test_topdown_sample() {
  const fs::path dir = fs::temp_directory_path() / "arcane_tilemap_topdown";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::copy(ARCANE_TOPDOWN_PROJECT_DIR, dir, fs::copy_options::recursive, ec);
  ARCANE_ASSERT_TRUE(!ec);
  CommandRegistry registry;
  register_all_commands(registry);
  {
    auto opened = ProjectContext::open(dir, ARCANE_TOPDOWN_MODULE_PATH);
    ARCANE_ASSERT_TRUE(opened.has_value());
    auto info = ok(registry, **opened, "tilemap.info", {{"node", "path:/Arena/Walls"}});
    ARCANE_ASSERT_TRUE(info.value["tileset_found"] == true && info.value["width"] == 20);
    const std::string before = dump(registry, **opened, 0, "path:/Arena/Walls");
    auto painted = ok(registry, **opened, "tilemap.erase", {{"node", "path:/Arena/Walls"}, {"x", 0}, {"y", 0}});
    ARCANE_ASSERT_TRUE(painted.inverse.has_value());
    ARCANE_ASSERT_TRUE(dump(registry, **opened, 0, "path:/Arena/Walls") != before);
    ok(registry, **opened, painted.inverse->command.c_str(), painted.inverse->args, {.persist = true});
    ARCANE_ASSERT_TRUE(dump(registry, **opened, 0, "path:/Arena/Walls") == before);
    auto reopened = ProjectContext::open(dir, ARCANE_TOPDOWN_MODULE_PATH);  // the saved legacy-era scene still loads
    ARCANE_ASSERT_TRUE(reopened.has_value() && dump(registry, **reopened, 0, "path:/Arena/Walls") == before);
  }
  fs::remove_all(dir, ec);
}

}  // namespace

int main() {
  test_cells_and_inverses();
  test_terrain_and_tileset_rules();
  test_stroke_undo_redo();
  test_topdown_sample();
  std::puts("test_tilemap_commands OK");
  return 0;
}
