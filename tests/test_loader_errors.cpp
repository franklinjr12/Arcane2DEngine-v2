// Table-driven bad-fixture test: every loader error starts with an RFC 6901
// JSON pointer ("#/nodes/2/id: ..."; root is "#: ..."), see docs/formats/errors.md.
// Add a row per failure mode; add a loader name to run_loader for new loaders.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/migration.hpp"
#include "arcane/project.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "test_assert.hpp"

namespace {

struct Case {
  const char* loader;
  std::string text;
  std::string expected_prefix;     // the pointer, e.g. "#/nodes/0/id"
  std::string expected_substring;  // part of the human-readable message
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  arcane::register_tilemap_components(registry);
  return registry;
}

template <class T>
std::string error_of(const std::expected<T, std::string>& result) {
  return result.has_value() ? std::string() : result.error();
}

// Returns the loader's error, or an empty string when loading succeeded.
std::string run_loader(const std::string& loader, const std::string& text, const arcane::ComponentRegistry& registry) {
  if (loader == "scene") return error_of(arcane::Scene::load_from_json(text, registry));
  if (loader == "scene_strict") return error_of(arcane::Scene::load_from_json_strict(text, registry));
  if (loader == "migrate_scene") return error_of(arcane::migrate_scene_json(text, registry));
  if (loader == "project") return error_of(arcane::load_project_from_json(text));
  if (loader == "project_strict") return error_of(arcane::load_project_from_json_strict(text));
  if (loader == "tileset") return error_of(arcane::load_tileset_from_json(text));
  if (loader == "tileset_strict") return error_of(arcane::load_tileset_from_json_strict(text));
  if (loader == "clip") return error_of(arcane::load_clip_from_json(text));
  if (loader == "clip_strict") return error_of(arcane::load_clip_from_json_strict(text));
  if (loader == "hud") return error_of(arcane::Hud::load_from_json(text));
  if (loader == "hud_strict") return error_of(arcane::Hud::load_from_json_strict(text));
  if (loader == "input") return error_of(arcane::InputActionMap::load_from_json(text));
  if (loader == "input_strict") return error_of(arcane::InputActionMap::load_from_json_strict(text));
  std::fprintf(stderr, "unknown loader '%s'\n", loader.c_str());
  std::exit(1);
}

// A v2 scene with one Sprite node; `extra_node_fields` / `component` let rows vary one spot.
std::string scene_json(const std::string& nodes, const std::string& header = R"("format":"arcane.scene","version":2,"next_id":10,)") {
  return "{" + header + R"("nodes":)" + nodes + "}";
}

std::string tilemap_scene(const std::string& properties) {
  return scene_json(R"([{"id":1,"name":"map","parent_id":0,"position":[0,0],"rotation_radians":0,"scale":[1,1],
    "components":[{"id":2,"type":"Tilemap","properties":{)" + properties + "}}]}]");
}

std::vector<Case> cases() {
  const std::string sprite_node =
      R"([{"id":1,"name":"a","parent_id":0,"position":[0,0],"rotation_radians":0,"scale":[1,1],
    "components":[{"id":2,"type":"Sprite","properties":{"width":"wide"}}]}])";
  return {
      // ---- scene ----
      {"scene", "{\"format\":", "#: ", "malformed scene JSON"},
      {"scene", R"({"format":"arcane.sprite","version":2,"nodes":[]})", "#/format: ", "expected \"arcane.scene\""},
      {"scene", R"({"format":"arcane.scene","version":3,"nodes":[]})", "#/version: ", "newer than supported"},
      {"scene", R"({"format":"arcane.scene","version":2,"next_id":1})", "#: ", "missing a 'nodes' array"},
      {"scene", scene_json(R"([{"id":1,"name":"a"},{"id":1,"name":"b"}])"), "#/nodes/1/id: ", "duplicate node id 1"},
      {"scene", scene_json(R"([{"id":1,"name":"a","components":[{"id":2,"type":"Nope"}]}])"),
       "#/nodes/0/components/0/type: ", "unknown component type 'Nope'"},
      {"scene_strict", scene_json(sprite_node), "#/nodes/0/components/0/properties/width: ",
       "expected a number"},
      {"scene_strict", scene_json(R"([{"id":1,"name":"a","colour":1}])"), "#/nodes/0/colour: ", "unknown field 'colour'"},
      {"scene", scene_json(R"([{"id":1,"name":"a","extra":1}])", R"("format":"arcane.scene","version":2,"zzz":1,)"),
       "#/zzz: ", "unknown field 'zzz'"},
      {"scene", scene_json(R"([{"id":1,"name":"a","parent_id":7}])"), "#/nodes/0/parent_id: ", "unknown parent_id 7"},
      {"scene", scene_json(R"([{"id":1,"name":"a","position":[0,"x"]}])"), "#/nodes/0/position/1: ", "expected a number"},
      {"scene", scene_json(R"([{"id":1,"name":"a","components":[{"id":2,"type":"Sprite","properties":[]}]}])"),
       "#/nodes/0/components/0/properties: ", "expected an object"},
      {"scene_strict", scene_json(R"([{"id":1,"name":"a","components":[{"id":2,"type":"Sprite","properties":{"bogus":1}}]}])"),
       "#/nodes/0/components/0/properties/bogus: ", "unknown property 'bogus'"},
      {"scene", tilemap_scene(R"("width":2,"height":2,"layers":[[[1,0],[0,-3]]])"),
       "#/nodes/0/components/0/properties/layers/0/1/1: ", "negative tile id"},
      {"scene", tilemap_scene(R"("width":2,"height":2,"layers":[[[1,0]]])"),
       "#/nodes/0/components/0/properties/layers/0: ", "expected 2 rows"},
      {"scene", tilemap_scene(R"("width":2,"height":2,"layers":[[[1,0],[0,1,1]]])"),
       "#/nodes/0/components/0/properties/layers/0/1: ", "tile ids"},
      {"scene_strict", tilemap_scene(R"("width":3,"height":2,"layers":"1,x,3")"),
       "#/nodes/0/components/0/properties/layers: ", "bad tile id"},
      {"migrate_scene", scene_json(R"([{"id":1,"name":"a","components":[{"id":1,"type":"Sprite"}]}])"),
       "#/nodes/0/components/0/id: ", "already used"},
      // ---- project ----
      {"project", "{\"name\":", "#: ", "malformed project JSON"},
      {"project", R"({"format":"arcane.project","version":3,"name":"p","main_scene":"a.json"})", "#/version: ",
       "newer than supported"},
      {"project_strict", R"({"format":"arcane.project","version":2,"name":"p","main_scene":"../x.json"})", "#/main_scene: ",
       "'..' segment"},
      {"project_strict", R"({"format":"arcane.project","version":2,"name":"p","main_scene":"a.json","extra":1})",
       "#/extra: ", "unknown field 'extra'"},
      {"project", R"({"format":"arcane.project","version":2,"name":5,"main_scene":"a.json"})", "#/name: ", "expected a string"},
      // ---- hud ----
      {"hud", "{\"elements\":", "#: ", "malformed HUD JSON"},
      {"hud", R"({"format":"arcane.hud","version":3,"elements":[]})", "#/version: ", "newer than supported"},
      {"hud", R"({"format":"arcane.hud","version":2,"elements":[{"type":"text","id":"a"},
        {"type":"button","id":"b","offset":[1,"x"]}]})", "#/elements/1/offset/1: ", "expected a number"},
      {"hud", R"({"format":"arcane.hud","version":2,"elements":[{"type":"text","id":"a","anchor":"middle"}]})",
       "#/elements/0/anchor: ", "unknown HUD anchor"},
      {"hud", R"({"format":"arcane.hud","version":2,"elements":[{"type":"text","id":"a"},{"type":"text","id":"a"}]})",
       "#/elements/1/id: ", "duplicate element id"},
      {"hud_strict", R"({"format_version":1,"elements":[{"type":"text","glow":1}]})", "#/elements/0/glow: ",
       "unknown field 'glow'"},
      // ---- input ----
      {"input", "[", "#: ", "malformed input map JSON"},
      {"input", R"({"format":"arcane.input","version":3,"actions":[]})", "#/version: ", "newer than supported"},
      {"input", R"({"format":"arcane.input","version":2,"actions":[{"name":"a","bindings":[{"device":"keyboard","code":1},
        {"device":"keyboard","code":"w"}]}]})", "#/actions/0/bindings/1/code: ", "expected an integer"},
      {"input", R"({"format":"arcane.input","version":2,"actions":[{"name":"a","bindings":[{"device":"joystick"}]}]})",
       "#/actions/0/bindings/0/device: ", "unknown input device"},
      {"input_strict", R"({"format_version":1,"actions":[{"name":"a","turbo":true}]})", "#/actions/0/turbo: ",
       "unknown field 'turbo'"},
      // ---- project: embedded documents are rebased ----
      {"project", R"({"format":"arcane.project","version":2,"name":"p","main_scene":"a.json",
        "hud":{"format":"arcane.hud","version":2,"elements":[{"type":"text","id":"t","offset":[0,"y"]}]}})",
       "#/hud/elements/0/offset/1: ", "expected a number"},
      {"project", R"({"format":"arcane.project","version":2,"name":"p","main_scene":"a.json",
        "input_actions":{"format":"arcane.input","version":2,"actions":[{"name":"a","bindings":[{"device":"nope"}]}]}})",
       "#/input_actions/actions/0/bindings/0/device: ", "unknown input device"},
      // ---- tileset ----
      {"tileset", "[1,", "#: ", "tileset: "},
      {"tileset", R"({"format":"arcane.clip","version":2})", "#/format: ", "expected \"arcane.tileset\""},
      {"tileset", R"({"format":"arcane.tileset","version":2,"texture":1,"tile_width":8,"tile_height":8,"columns":2,
        "tile_count":4,"tiles":[{"solid":true},{"terrain":"x"}]})", "#/tiles/1/terrain: ", "expected an integer"},
      {"tileset_strict", R"({"format":"arcane.tileset","version":2,"texture":1,"tile_width":8,"tile_height":8,"columns":2,
        "tile_count":4,"bogus":1})", "#/bogus: ", "unknown field 'bogus'"},
      {"tileset", R"({"format":"arcane.tileset","version":2,"texture":1,"tile_width":8,"tile_height":8,"columns":2,
        "tile_count":4,"terrains":[{"terrain":1,"tiles":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"q"]}]})",
       "#/terrains/0/tiles/15: ", "expected an integer"},
      // ---- clip ----
      {"clip", "{", "#: ", "animation clip"},
      {"clip", R"({"format":"arcane.tileset","version":2,"frames":[]})", "#/format: ", "expected \"arcane.clip\""},
      {"clip", R"({"format":"arcane.clip","version":2,"frames":[{"uv":[0,0,1,1]},{"uv":[0,0,1,1],"duration_seconds":"slow"}]})",
       "#/frames/1/duration_seconds: ", "expected a number"},
      {"clip", R"({"format":"arcane.clip","version":2,"frames":[{"uv":[0,0,"a",1]}]})", "#/frames/0/uv/2: ", "expected a number"},
      {"clip_strict", R"({"format":"arcane.clip","version":2,"frames":[],"speed":2})", "#/speed: ", "unknown field 'speed'"},
  };
}

struct FileCase {
  const char* file;       // project-relative file overwritten with bad content
  std::string text;
  std::string expected;   // "<file>#<pointer>" prefix of the migrate_project error
};

// Copies the golden sample project to a temp dir, overwrites one file per row
// and checks that migrate_project names the file and the JSON pointer.
int run_file_cases(const arcane::ComponentRegistry& registry) {
  namespace fs = std::filesystem;
  const std::vector<FileCase> rows = {
      {"project.json", R"({"format":"arcane.project","version":2,"name":3,"main_scene":"scenes/main.scene.json"})",
       "project.json#/name"},
      {"project.json", "{", "project.json#"},
      {"scenes/main.scene.json", R"({"format":"arcane.scene","version":2,"next_id":2,"nodes":[{"id":"x"}]})",
       "scenes/main.scene.json#/nodes/0"},
      {"assets/tiles.json", R"({"format":"arcane.tileset","version":2,"texture":1,"tile_width":8,"tile_height":8,
        "columns":2,"tile_count":4,"tiles":[{"terrain":"x"}]})",
       "assets/tiles.json#/tiles/0/terrain"},
      {"assets/bad.clip.json", R"({"format":"arcane.clip","version":2,"frames":[{"uv":[0,0,"a",1]}]})",
       "assets/bad.clip.json#/frames/0/uv/2"},
  };
  int failures = 0;
  int index = 0;
  for (const FileCase& row : rows) {
    const fs::path dir = fs::temp_directory_path() / ("arcane_loader_errors_" + std::to_string(index++));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::copy(ARCANE_GOLDEN_DIR "/sample_project", dir, fs::copy_options::recursive, ec);
    bool ok = !ec;
    if (ok) {
      // The sample scene needs module components; use an empty one so only the bad row can fail.
      std::ofstream scene(dir / "scenes/main.scene.json", std::ios::binary | std::ios::trunc);
      scene << R"({"format":"arcane.scene","version":2,"next_id":1,"nodes":[]})";
      scene.close();
      std::ofstream out(dir / row.file, std::ios::binary | std::ios::trunc);
      out << row.text;
      out.close();
      auto result = arcane::migrate_project(dir, registry);
      ok = !result.has_value() && result.error().rfind(row.expected, 0) == 0;
      if (!ok) {
        std::fprintf(stderr, "FILE CASE FAILED [%s] expected '%s...'\n  got: %s\n", row.file, row.expected.c_str(),
                     result ? "(migrated fine)" : result.error().c_str());
      }
    }
    fs::remove_all(dir, ec);
    if (!ok) ++failures;
  }
  return failures;
}

}  // namespace

int main() {
  const arcane::ComponentRegistry registry = make_registry();
  int failures = 0;
  for (const Case& c : cases()) {
    const std::string error = run_loader(c.loader, c.text, registry);
    const bool failed = !error.empty();
    const bool prefix_ok = error.rfind(c.expected_prefix, 0) == 0;
    const bool substring_ok = error.find(c.expected_substring) != std::string::npos;
    if (!failed || !prefix_ok || !substring_ok) {
      std::fprintf(stderr, "CASE FAILED [%s] expected '%s...' containing '%s'\n  got: %s\n", c.loader,
                   c.expected_prefix.c_str(), c.expected_substring.c_str(), failed ? error.c_str() : "(loaded fine)");
      ++failures;
    }
  }
  failures += run_file_cases(registry);
  ARCANE_ASSERT_TRUE(failures == 0);
  return 0;
}
