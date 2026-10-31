// Table-driven bad-fixture test: every loader error starts with an RFC 6901
// JSON pointer ("#/nodes/2/id: ..."; root is "#: ..."), see docs/formats/errors.md.
// Add a row per failure mode; add a loader name to run_loader for new loaders.
#include <cstdio>
#include <string>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/builtin_components.hpp"
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
  };
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
  ARCANE_ASSERT_TRUE(failures == 0);
  return 0;
}
