// Canonical v2 for tilesets, clips, the nested HUD/input documents and the
// structured Tilemap rows: legacy readers, strict readers, semantic
// equality across migration, repeat canonicalization, unsupported versions.
#include <string>

#include "arcane/animation.hpp"
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

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

template <class T>
bool fails_with(const std::expected<T, std::string>& result, const std::string& part) {
  return !result.has_value() && contains(result.error(), part);
}

// ---- clips -----------------------------------------------------------------

void test_clip() {
  const std::string legacy =
      R"({"format_version":1,"name":"run","loop":false,"frames":[{"uv":[0,0,0.25,1],"duration_seconds":0.5},{"uv":[0.25,0,0.5,1]}]})";
  auto clip = arcane::load_clip_from_json(legacy);
  ARCANE_ASSERT_TRUE(clip && clip->frames.size() == 2 && !clip->loop && clip->frames[1].duration_seconds == 0.1f);

  auto v2 = arcane::migrate_clip_json(legacy);
  ARCANE_ASSERT_TRUE(v2.has_value());
  ARCANE_ASSERT_TRUE(contains(*v2, "\"format\": \"arcane.clip\"") && !contains(*v2, "format_version"));
  auto again = arcane::load_clip_from_json(*v2);
  ARCANE_ASSERT_TRUE(again && again->name == "run" && again->frames[0].uv.u1 == 0.25f &&
                     again->frames[0].duration_seconds == 0.5f && again->frames[1].duration_seconds == 0.1f);
  ARCANE_ASSERT_TRUE(arcane::migrate_clip_json(*v2).value() == *v2);  // repeat run: same bytes
  ARCANE_ASSERT_TRUE(arcane::canonicalize_json(*v2).value() == *v2);

  // Unknown fields: lenient for legacy loads, errors for strict/v2/migration.
  const std::string extra = R"({"format_version":1,"name":"x","frames":[],"tag":"keep me"})";
  ARCANE_ASSERT_TRUE(arcane::load_clip_from_json(extra).has_value());
  ARCANE_ASSERT_TRUE(fails_with(arcane::migrate_clip_json(extra), "tag"));
  ARCANE_ASSERT_TRUE(fails_with(
      arcane::load_clip_from_json(R"({"format":"arcane.clip","version":2,"frames":[{"uv":[0,0,1,1],"hold":1}]})"),
      "hold"));
  ARCANE_ASSERT_TRUE(fails_with(
      arcane::load_clip_from_json(R"({"format":"arcane.clip","version":2,"frames":[{"uv":[0,0,1,1],"duration_seconds":-1}]})"),
      "negative"));
  // Versions: future and wrong-kind headers are refused, never interpreted.
  ARCANE_ASSERT_TRUE(fails_with(arcane::load_clip_from_json(R"({"format":"arcane.clip","version":3,"frames":[]})"), "newer"));
  ARCANE_ASSERT_TRUE(!arcane::load_clip_from_json(R"({"format":"arcane.tileset","version":2,"frames":[]})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::load_clip_from_json(R"({"format_version":2,"frames":[]})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::load_clip_from_json(R"({"format_version":1,"frames":[],"frames":[]})").has_value());
}

// ---- tilesets (FE9 terrain) -------------------------------------------------

void test_tileset() {
  const std::string legacy =
      R"({"format_version":1,"texture":18446744073709551615,"tile_width":16,"tile_height":8,"columns":4,"tile_count":6,
          "tiles":[{"solid":true,"terrain":1},{"terrain":1},{"solid":true}],
          "terrains":[{"terrain":1,"tiles":[1,2,0,3,0,0,0,0,0,0,0,0,0,0,0,2]}]})";
  auto old = arcane::load_tileset_from_json(legacy);
  ARCANE_ASSERT_TRUE(old.has_value());
  auto v2 = arcane::migrate_tileset_json(legacy);
  ARCANE_ASSERT_TRUE(v2.has_value());
  auto now = arcane::load_tileset_from_json(*v2);
  ARCANE_ASSERT_TRUE(now.has_value());

  // Same meaning before and after, including the 64-bit texture id and FE9 terrain lookups.
  ARCANE_ASSERT_TRUE(now->texture == 18446744073709551615ull && now->tile_width == 16 && now->tile_height == 8);
  ARCANE_ASSERT_TRUE(now->columns == 4 && now->tile_count == 6 && now->tiles.size() == 3);
  ARCANE_ASSERT_TRUE(now->tiles[0].solid && now->tiles[0].terrain == 1 && !now->tiles[1].solid && !now->tiles[2].terrain);
  for (int mask = 0; mask < 16; ++mask) {
    ARCANE_ASSERT_TRUE(now->tile_for_mask(1, mask) == old->tile_for_mask(1, mask));
  }
  ARCANE_ASSERT_TRUE(now->tile_for_mask(1, 1) == 2 && now->tile_for_mask(1, 3) == 0 && now->terrain_of(2) == 1 && now->terrain_of(3) == 0);
  ARCANE_ASSERT_TRUE(arcane::migrate_tileset_json(*v2).value() == *v2);
  ARCANE_ASSERT_TRUE(arcane::save_tileset_canonical_json(*now).value() == *v2);

  // A tileset without tiles/terrains (the original v1 minimum) migrates too.
  auto minimal = arcane::migrate_tileset_json(
      R"({"format_version":1,"texture":1,"tile_width":16,"tile_height":16,"columns":2,"tile_count":4})");
  ARCANE_ASSERT_TRUE(minimal && contains(*minimal, "\"terrains\": []") && contains(*minimal, "\"tiles\": []"));

  // Strictness and versions.
  ARCANE_ASSERT_TRUE(fails_with(arcane::migrate_tileset_json(
      R"({"format_version":1,"texture":1,"tile_width":16,"tile_height":16,"columns":2,"tile_count":4,"note":"x"})"),
                                "note"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::load_tileset_from_json(
      R"({"format":"arcane.tileset","version":2,"texture":1,"tile_width":16,"tile_height":16,"columns":2,"tile_count":2,
          "terrains":[{"terrain":1,"tiles":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,9]}]})"),
                                "outside"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::load_tileset_from_json(
      R"({"format":"arcane.tileset","version":9,"texture":1,"tile_width":16,"tile_height":16,"columns":2,"tile_count":2})"),
                                "newer"));
  ARCANE_ASSERT_TRUE(!arcane::load_tileset_from_json(R"({"format_version":2})").has_value());
}

// ---- tile layer rows ----------------------------------------------------------

arcane::ComponentRegistry tile_registry() {
  arcane::ComponentRegistry registry;
  arcane::register_tilemap_components(registry);
  return registry;
}

std::string scene_with_layers(const std::string& properties) {
  return R"({"format":"arcane.scene","version":2,"next_id":3,"nodes":[{"id":1,"name":"map","parent_id":0,
    "position":[0,0],"rotation_radians":0,"scale":[1,1],
    "components":[{"id":2,"type":"Tilemap","properties":{)" + properties + "}}]}]}";
}

arcane::Tilemap* tilemap_of(arcane::Scene& scene) {
  return static_cast<arcane::Tilemap*>(scene.roots()[0]->find_component("Tilemap"));
}

void test_tile_rows() {
  const auto registry = tile_registry();
  // Rows are arrays, and "layers" sorts before "width"/"height": order must not matter.
  const std::string rows =
      scene_with_layers(R"("layers":[[[1,0,2],[0,3,0]],[[0,0,0],[0,0,4]]],"width":3,"height":2,"solid_tiles":"1,2")");
  auto scene = arcane::Scene::load_from_json(rows, registry);
  ARCANE_ASSERT_TRUE(scene.has_value());
  auto* map = tilemap_of(*scene);
  ARCANE_ASSERT_TRUE(map->layer_count() == 2 && map->tile_at(0, 0, 0) == 1 && map->tile_at(0, 2, 0) == 2 &&
                     map->tile_at(0, 1, 1) == 3 && map->tile_at(1, 2, 1) == 4 && map->tile_at(1, 0, 0) == 0);

  const std::string saved = scene->save_canonical_json(registry).value();
  ARCANE_ASSERT_TRUE(!contains(saved, "\"layers\": \"") && contains(saved, "\"layers\": ["));
  ARCANE_ASSERT_TRUE(contains(saved, "\"solid_tiles\": \"1,2\""));
  auto reloaded = arcane::Scene::load_from_json(saved, registry);
  ARCANE_ASSERT_TRUE(reloaded.has_value());
  ARCANE_ASSERT_TRUE(reloaded->save_canonical_json(registry).value() == saved);
  ARCANE_ASSERT_TRUE(arcane::migrate_scene_json(saved, registry).value() == saved);

  // The legacy text form (v1 and the first v2 files) migrates to the same rows.
  const std::string text_form = scene_with_layers(R"("layers":"1,0,2,0,3,0;0,0,0,0,0,4","width":3,"height":2,"solid_tiles":"1,2")");
  auto migrated = arcane::migrate_scene_json(text_form, registry);
  ARCANE_ASSERT_TRUE(migrated.has_value() && *migrated == saved);
  // A short layer (missing trailing ids read as 0) pads out to full rows.
  auto short_layer = arcane::migrate_scene_json(scene_with_layers(R"("layers":"1,2","width":3,"height":2)"), registry);
  ARCANE_ASSERT_TRUE(short_layer && contains(*short_layer, "\"layers\": ["));
  auto short_scene = arcane::Scene::load_from_json(*short_layer, registry);
  ARCANE_ASSERT_TRUE(short_scene && tilemap_of(*short_scene)->tile_at(0, 1, 0) == 2);

  // Wrong shapes are errors, never silently padded or cut.
  ARCANE_ASSERT_TRUE(fails_with(arcane::Scene::load_from_json(
      scene_with_layers(R"("layers":[[[1,0],[0,3,0]]],"width":3,"height":2)"), registry), "tile ids"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::Scene::load_from_json(
      scene_with_layers(R"("layers":[[[1,0,0]]],"width":3,"height":2)"), registry), "rows"));
  ARCANE_ASSERT_TRUE(!arcane::Scene::load_from_json(
      scene_with_layers(R"("layers":[[[1,-1,0],[0,0,0]]],"width":3,"height":2)"), registry).has_value());
  ARCANE_ASSERT_TRUE(!arcane::Scene::load_from_json(
      scene_with_layers(R"("layers":[[[1.5,0,0],[0,0,0]]],"width":3,"height":2)"), registry).has_value());
  ARCANE_ASSERT_TRUE(fails_with(arcane::migrate_scene_json(
      scene_with_layers(R"("layers":"1,2,3,4,5,6,7","width":3,"height":2)"), registry), "do not fit"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::migrate_scene_json(
      scene_with_layers(R"("layers":"1,x,3","width":3,"height":2)"), registry), "bad tile id"));
  // Legacy leniency stays for plain loads of the old text form.
  std::string v1_scene = scene_with_layers(R"("layers":"1,x,3,4,5,6,7","width":3,"height":2)");
  v1_scene.replace(v1_scene.find(R"("format":"arcane.scene","version":2)"), 35, R"("format_version":1)");
  ARCANE_ASSERT_TRUE(arcane::Scene::load_from_json(v1_scene, registry).has_value());
  ARCANE_ASSERT_TRUE(!arcane::migrate_scene_json(v1_scene, registry).has_value());
}

// ---- HUD -----------------------------------------------------------------------

void test_hud() {
  const std::string legacy = R"({"format_version":1,"elements":[
    {"type":"text","text":"SCORE","anchor":"top_left","offset":[16,16],"font_scale":3,"color":[1,1,1,1]},
    {"type":"progress_bar","anchor":"top_right","offset":[-16,16],"size":[220,20],"fraction":1},
    {"type":"text","text":"","anchor":"center"},
    {"type":"button","id":"text_2","label":"GO"},
    {"type":"button","label":"NOID"}]})";
  auto hud = arcane::Hud::load_from_json(legacy);
  ARCANE_ASSERT_TRUE(hud.has_value() && hud->elements().size() == 5);
  // Ids: explicit ones win, the rest are assigned in file order and never collide.
  ARCANE_ASSERT_TRUE(std::get<arcane::HudText>(hud->elements()[0]).id == "text_1");
  ARCANE_ASSERT_TRUE(std::get<arcane::HudProgressBar>(hud->elements()[1]).id == "bar_1");
  ARCANE_ASSERT_TRUE(std::get<arcane::HudText>(hud->elements()[2]).id == "text_3");
  ARCANE_ASSERT_TRUE(std::get<arcane::HudButton>(hud->elements()[3]).id == "text_2");
  ARCANE_ASSERT_TRUE(std::get<arcane::HudButton>(hud->elements()[4]).id == "button_1");

  auto v2 = hud->save_canonical_json();
  ARCANE_ASSERT_TRUE(v2.has_value() && contains(*v2, "\"format\": \"arcane.hud\"") && !contains(*v2, "format_version"));
  auto back = arcane::Hud::load_from_json(*v2);
  ARCANE_ASSERT_TRUE(back.has_value() && back->save_canonical_json().value() == *v2);  // repeat: same bytes
  ARCANE_ASSERT_TRUE(std::get<arcane::HudText>(back->elements()[0]).font_scale == 3.0f);
  ARCANE_ASSERT_TRUE(std::get<arcane::HudButton>(back->elements()[4]).anchor == arcane::HudAnchor::Center);
  // Order is the draw order and survives.
  ARCANE_ASSERT_TRUE(back->elements().size() == 5 && back->elements()[3].index() == 3 && back->elements()[1].index() == 2);

  // Images are persisted by asset id (the runtime texture pointer is not).
  arcane::Hud with_image;
  arcane::HudImage image;
  image.texture_asset = 6606372916662563687ull;
  image.uv = {0.0f, 0.25f, 0.5f, 0.75f};
  image.anchor = arcane::HudAnchor::BottomRight;
  image.offset_x = -8.0f;
  image.width = 48.0f;
  image.tint = {1.0f, 0.5f, 0.25f, 1.0f};
  with_image.add(image);
  with_image.add(image);  // empty ids -> image_1, image_2
  auto image_text = with_image.save_canonical_json();
  ARCANE_ASSERT_TRUE(image_text.has_value() && contains(*image_text, "\"asset\": 6606372916662563687"));
  auto image_back = arcane::Hud::load_from_json(*image_text);
  ARCANE_ASSERT_TRUE(image_back.has_value() && image_back->elements().size() == 2);
  const auto& loaded_image = std::get<arcane::HudImage>(image_back->elements()[1]);
  ARCANE_ASSERT_TRUE(loaded_image.id == "image_2" && loaded_image.texture_asset == 6606372916662563687ull &&
                     loaded_image.texture == nullptr && loaded_image.uv.v1 == 0.75f &&
                     loaded_image.anchor == arcane::HudAnchor::BottomRight && loaded_image.offset_x == -8.0f &&
                     loaded_image.width == 48.0f && loaded_image.height == 32.0f && loaded_image.tint.g == 0.5f);

  // Editor-style duplicates ("button" added twice) are made unique on save.
  arcane::Hud dup;
  dup.add(arcane::HudButton{.id = "button", .label = "A"});
  dup.add(arcane::HudButton{.id = "button", .label = "B"});
  auto dup_back = arcane::Hud::load_from_json(dup.save_canonical_json().value());
  ARCANE_ASSERT_TRUE(dup_back && std::get<arcane::HudButton>(dup_back->elements()[0]).id == "button" &&
                     std::get<arcane::HudButton>(dup_back->elements()[1]).id == "button_1");

  // Strict v2 and migration.
  ARCANE_ASSERT_TRUE(fails_with(arcane::Hud::load_from_json(
      R"({"format":"arcane.hud","version":2,"elements":[{"type":"text","text":"x"}]})"), "id"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::Hud::load_from_json(
      R"({"format":"arcane.hud","version":2,"elements":[{"type":"text","id":"a"},{"type":"text","id":"a"}]})"), "duplicate"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::Hud::load_from_json(
      R"({"format":"arcane.hud","version":2,"elements":[{"type":"text","id":"a","glow":1}]})"), "glow"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::Hud::load_from_json_strict(
      R"({"format_version":1,"elements":[{"type":"button","id":"a"},{"type":"button","id":"a"}]})"), "duplicate"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::Hud::load_from_json_strict(
      R"({"format_version":1,"elements":[{"type":"text","shadow":true}]})"), "shadow"));
  ARCANE_ASSERT_TRUE(arcane::Hud::load_from_json(R"({"format_version":1,"elements":[{"type":"text","shadow":true}]})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::Hud::load_from_json(R"({"format":"arcane.hud","version":3,"elements":[]})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::Hud::load_from_json(R"({"format_version":2,"elements":[]})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::Hud::load_from_json(R"({"format_version":1,"elements":[{"type":"nope"}]})").has_value());
}

// ---- input -----------------------------------------------------------------------

void test_input() {
  // A pre-FE8 map: no pad/axis/player fields. Defaults must come back unchanged.
  const std::string old_map =
      R"({"format_version":1,"actions":[{"name":"jump","bindings":[{"device":"keyboard","code":32},{"device":"gamepad_button","code":0}]},
          {"name":"steer","bindings":[{"device":"gamepad_axis","code":0}]}]})";
  auto map = arcane::InputActionMap::load_from_json_strict(old_map);
  ARCANE_ASSERT_TRUE(map.has_value() && map->actions().size() == 2);
  const arcane::InputBinding& pad = map->actions()[0].bindings[1];
  ARCANE_ASSERT_TRUE(pad.pad == -1 && pad.axis_sign == 0 && map->actions()[0].player == -1);
  const arcane::InputBinding& axis = map->actions()[1].bindings[0];
  ARCANE_ASSERT_TRUE(axis.deadzone == 0.2f && axis.threshold == 0.5f && axis.axis_sign == 0);

  auto v2 = map->save_canonical_json();
  ARCANE_ASSERT_TRUE(v2.has_value() && contains(*v2, "\"format\": \"arcane.input\"") && !contains(*v2, "format_version"));
  ARCANE_ASSERT_TRUE(!contains(*v2, "\"pad\"") && !contains(*v2, "\"player\""));  // defaults stay omitted
  auto back = arcane::InputActionMap::load_from_json(*v2);
  ARCANE_ASSERT_TRUE(back.has_value() && back->actions().size() == 2 && back->actions()[1].bindings[0] == axis);
  ARCANE_ASSERT_TRUE(back->actions()[0].bindings[0].code == 32 && back->save_canonical_json().value() == *v2);

  // Non-default FE8 fields round trip.
  arcane::InputActionMap rich;
  arcane::InputAction action;
  action.name = "move";
  action.player = 1;
  action.bindings.push_back({arcane::InputDevice::GamepadAxis, 0, 2, -1, 0.3f, 0.7f});
  rich.set_action(action);
  auto rich_back = arcane::InputActionMap::load_from_json(rich.save_canonical_json().value());
  ARCANE_ASSERT_TRUE(rich_back && rich_back->actions()[0].player == 1 && rich_back->actions()[0].bindings[0] == action.bindings[0]);

  // Out-of-range values: clamped by plain legacy loads (as before), refused by strict reads.
  const std::string wild = R"({"format_version":1,"actions":[{"name":"a","player":9,"bindings":[]}]})";
  auto clamped = arcane::InputActionMap::load_from_json(wild);
  ARCANE_ASSERT_TRUE(clamped && clamped->actions()[0].player == arcane::kMaxGamepads - 1);
  ARCANE_ASSERT_TRUE(fails_with(arcane::InputActionMap::load_from_json_strict(wild), "outside"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::InputActionMap::load_from_json(
      R"({"format":"arcane.input","version":2,"actions":[{"name":"a","bindings":[]},{"name":"a","bindings":[]}]})"), "duplicate"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::InputActionMap::load_from_json(
      R"({"format":"arcane.input","version":2,"actions":[{"name":"a","extra":1}]})"), "extra"));
  ARCANE_ASSERT_TRUE(fails_with(arcane::InputActionMap::load_from_json(
      R"({"format":"arcane.input","version":2,"actions":[{"name":"a","bindings":[{"device":"keyboard","code":1,"deadzone":0.5}]}]})"),
                                "axis fields"));
  ARCANE_ASSERT_TRUE(!arcane::InputActionMap::load_from_json(R"({"format":"arcane.input","version":3,"actions":[]})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::InputActionMap::load_from_json(R"({"format_version":2,"actions":[]})").has_value());
}

// ---- project with nested documents ----------------------------------------------------

void test_project_nested() {
  const std::string legacy = R"({"format_version":1,"name":"p","main_scene":"scenes/main.scene.json","module":"",
    "input_actions":{"format_version":1,"actions":[{"name":"jump","bindings":[{"device":"keyboard","code":32}]}]},
    "hud":{"format_version":1,"elements":[{"type":"text","text":"hi"}]}})";
  auto v2 = arcane::migrate_project_json(legacy);
  ARCANE_ASSERT_TRUE(v2.has_value());
  ARCANE_ASSERT_TRUE(contains(*v2, "\"format\": \"arcane.hud\"") && contains(*v2, "\"format\": \"arcane.input\""));
  ARCANE_ASSERT_TRUE(!contains(*v2, "format_version"));
  ARCANE_ASSERT_TRUE(arcane::migrate_project_json(*v2).value() == *v2);
  auto project = arcane::load_project_from_json(*v2);
  ARCANE_ASSERT_TRUE(project && project->input_actions.actions().size() == 1 && project->hud.elements().size() == 1);
  ARCANE_ASSERT_TRUE(std::get<arcane::HudText>(project->hud.elements()[0]).id == "text_1");

  // A first-generation v2 project (header v2, nested documents still format_version 1) still reads and upgrades.
  const std::string first_v2 = R"({"format":"arcane.project","version":2,"name":"p","main_scene":"s.scene.json","module":"",
    "input_actions":{"format_version":1,"actions":[]},"hud":{"format_version":1,"elements":[]}})";
  ARCANE_ASSERT_TRUE(arcane::load_project_from_json(first_v2).has_value());
  ARCANE_ASSERT_TRUE(contains(arcane::migrate_project_json(first_v2).value(), "arcane.hud"));

  // Unknown data inside a nested document is refused, not dropped.
  ARCANE_ASSERT_TRUE(!arcane::migrate_project_json(R"({"format_version":1,"name":"p","main_scene":"s.scene.json",
    "hud":{"format_version":1,"elements":[{"type":"text","blur":2}]}})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::migrate_project_json(R"({"format_version":1,"name":"p","main_scene":"s.scene.json",
    "input_actions":{"format_version":3,"actions":[]}})").has_value());
}

void test_project_display() {
  const std::string head = R"({"format":"arcane.project","version":2,"name":"p","main_scene":"s.scene.json","module":"")";
  auto ok = arcane::load_project_from_json_strict(head + R"(,"display":{"design_width":640,"design_height":360,"scale_mode":"stretch"}})");
  ARCANE_ASSERT_TRUE(ok.has_value() && ok->display.design_width == 640 && ok->display.design_height == 360 &&
                     ok->display.scale_mode == arcane::DisplayScaleMode::Stretch);
  auto text = arcane::save_project_canonical_json(*ok);
  ARCANE_ASSERT_TRUE(text.has_value() && contains(*text, "\"display\"") && contains(*text, "\"stretch\""));
  auto again = arcane::load_project_from_json_strict(*text);
  ARCANE_ASSERT_TRUE(again.has_value() && again->display.design_width == 640);
  // Defaults are not written.
  arcane::ProjectFile plain;
  plain.name = "p";
  plain.main_scene = "s.scene.json";
  ARCANE_ASSERT_TRUE(!contains(arcane::save_project_canonical_json(plain).value(), "display"));
  // Rejections.
  for (const char* bad : {R"(,"display":{"design_width":640,"design_height":0}})", R"(,"display":{"design_width":8,"design_height":8}})",
                          R"(,"display":{"design_width":9000,"design_height":360}})", R"(,"display":{"scale_mode":"zoom"}})",
                          R"(,"display":{"blur":1}})", R"(,"display":[]})"}) {
    ARCANE_ASSERT_TRUE(!arcane::load_project_from_json_strict(head + bad).has_value());
  }
  arcane::ProjectFile broken = plain;
  broken.display.design_width = 640;
  ARCANE_ASSERT_TRUE(!arcane::save_project_canonical_json(broken).has_value());
}

}  // namespace

int main() {
  test_clip();
  test_tileset();
  test_tile_rows();
  test_hud();
  test_input();
  test_project_nested();
  test_project_display();
  return 0;
}
