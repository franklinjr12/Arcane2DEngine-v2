// Exercises arcane_editor_core's ProjectSession (open/create/save) and
// asset_catalog, plus the inspector-style registry walk over a real game
// module's component (not just a type set up in-test): loads sample_game,
// registers both built-in (Sprite/Camera) and module (Spinner) types into
// one registry, same as arcane_editor does at startup.
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/module.hpp"
#include "arcane/node.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "test_assert.hpp"

namespace {

arcane::ComponentRegistry make_registry_with_module(arcane::GameModule& module) {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  module.api()->register_types(registry);
  return registry;
}

// Inspector panels walk every registered type's PropertyDescriptors (see
// editor/app/editor_main.cpp's draw_property_editor); this is the headless
// equivalent: built-in types and a real module's type must all be
// discoverable and have working get/set through the same descriptors.
void test_registry_walk_covers_builtin_and_module_types() {
  auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(module.has_value());
  arcane::ComponentRegistry registry = make_registry_with_module(*module);

  const auto names = registry.type_names();
  auto has = [&](const char* n) { return std::find(names.begin(), names.end(), n) != names.end(); };
  ARCANE_ASSERT_TRUE(has("Sprite"));
  ARCANE_ASSERT_TRUE(has("Camera"));
  ARCANE_ASSERT_TRUE(has("Spinner"));

  arcane::Scene scene;
  arcane::Node* node = scene.create_node("n");
  arcane::Component* spinner = scene.add_component(node, "Spinner", registry);
  ARCANE_ASSERT_TRUE(spinner != nullptr);

  const auto* descriptors = registry.properties_of("Spinner");
  ARCANE_ASSERT_TRUE(descriptors != nullptr);
  bool set_any = false;
  for (const auto& descriptor : *descriptors) {
    if (descriptor.type == arcane::PropertyType::Float) {
      descriptor.set(*spinner, arcane::PropertyValue{float{7.5f}});
      ARCANE_ASSERT_TRUE(std::get<float>(descriptor.get(*spinner)) == 7.5f);
      set_any = true;
    }
  }
  ARCANE_ASSERT_TRUE(set_any);
}

// The core Save/reopen contract from the vision doc's Foundation C
// acceptance conditions: a project with an edited component property must
// come back with that exact value after a full close + reopen, using only
// ProjectSession (no test-only shortcuts).
void test_project_session_save_reopen_keeps_edits() {
  auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(module.has_value());
  arcane::ComponentRegistry registry = make_registry_with_module(*module);

  const auto project_dir = std::filesystem::temp_directory_path() / "arcane_test_editor_project";
  std::filesystem::remove_all(project_dir);

  auto created = arcane::editor::ProjectSession::create(project_dir, "test_project", registry);
  ARCANE_ASSERT_TRUE(created.has_value());
  ARCANE_ASSERT_TRUE(!created->dirty());

  arcane::Node* node = created->scene().create_node("Player");
  arcane::Component* spinner = created->scene().add_component(node, "Spinner", registry);
  const auto* descriptors = registry.properties_of("Spinner");
  const arcane::PropertyDescriptor* label_property = nullptr;
  for (const auto& d : *descriptors) {
    if (d.name == "label") label_property = &d;
  }
  ARCANE_ASSERT_TRUE(label_property != nullptr);
  label_property->set(*spinner, arcane::PropertyValue{std::string("edited-label")});
  created->mark_dirty();

  const arcane::NodeId node_id = node->id();
  auto saved = created->save(registry);
  ARCANE_ASSERT_TRUE(saved.has_value());
  ARCANE_ASSERT_TRUE(!created->dirty());

  auto reopened = arcane::editor::ProjectSession::open(project_dir, registry);
  ARCANE_ASSERT_TRUE(reopened.has_value());
  arcane::Node* reopened_node = reopened->scene().find_node(node_id);
  ARCANE_ASSERT_TRUE(reopened_node != nullptr);
  arcane::Component* reopened_spinner = reopened_node->find_component("Spinner");
  ARCANE_ASSERT_TRUE(reopened_spinner != nullptr);
  ARCANE_ASSERT_TRUE(std::get<std::string>(label_property->get(*reopened_spinner)) == "edited-label");

  std::filesystem::remove_all(project_dir);
}

void test_asset_catalog_ids_are_stable_and_listable() {
  const auto assets_dir = std::filesystem::temp_directory_path() / "arcane_test_asset_catalog";
  std::filesystem::remove_all(assets_dir);
  std::filesystem::create_directories(assets_dir / "sub");

  {
    std::ofstream(assets_dir / "a.png") << "not a real png, just needs to exist";
    std::ofstream(assets_dir / "sub" / "b.PNG") << "same here";
    std::ofstream(assets_dir / "notes.txt") << "should be excluded";
  }

  const auto entries = arcane::editor::list_image_assets(assets_dir);
  ARCANE_ASSERT_TRUE(entries.size() == 2);
  for (const auto& entry : entries) {
    ARCANE_ASSERT_TRUE(entry.id == arcane::editor::asset_id_for_relative_path(entry.relative_path));
  }
  // Same relative path must always hash to the same id (stability across
  // reopen, with no id->path table persisted -- see docs/decisions.md).
  ARCANE_ASSERT_TRUE(arcane::editor::asset_id_for_relative_path("a.png") ==
                      arcane::editor::asset_id_for_relative_path("a.png"));
  ARCANE_ASSERT_TRUE(arcane::editor::asset_id_for_relative_path("a.png") !=
                      arcane::editor::asset_id_for_relative_path("sub/b.PNG"));

  std::filesystem::remove_all(assets_dir);
}

void test_strict_runtime_rejects_unknown_legacy_module_property() {
  auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(module.has_value());
  auto registry = make_registry_with_module(*module);
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_strict_runtime";
  std::filesystem::remove_all(dir);
  auto created = arcane::editor::ProjectSession::create(dir, "strict test", registry);
  ARCANE_ASSERT_TRUE(created.has_value());
  const auto path = created->main_scene_path();
  {
    std::ofstream out(path);
    out << R"({"format_version":1,"next_id":3,"nodes":[{"id":1,"name":"n","parent_id":0,"position":[0,0],"rotation_radians":0,"scale":[1,1],"components":[{"id":2,"type":"Spinner","properties":{"unknown_future_property":42}}]}]})";
  }
  auto compatible = arcane::editor::ProjectRuntime::open(dir, ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(compatible.has_value());
  compatible->session().mark_dirty();
  auto saved = compatible->session().save(compatible->registry());
  ARCANE_ASSERT_TRUE(!saved.has_value());
  ARCANE_ASSERT_TRUE(saved.error().find("unknown_future_property") != std::string::npos);
  ARCANE_ASSERT_TRUE(compatible->session().dirty());
  auto strict = arcane::editor::ProjectRuntime::open(dir, ARCANE_SAMPLE_GAME_PATH,
                                                      arcane::editor::SceneLoadPolicy::Strict);
  ARCANE_ASSERT_TRUE(!strict.has_value());
  ARCANE_ASSERT_TRUE(strict.error().find("unknown_future_property") != std::string::npos);
  // Failed strict loading does not rewrite the source file.
  std::ifstream in(path);
  const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  ARCANE_ASSERT_TRUE(contents.find("unknown_future_property") != std::string::npos);
  std::filesystem::remove_all(dir);
}

void test_compatible_project_unknown_fields_cannot_be_saved() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_unknown_project_save";
  std::filesystem::remove_all(dir);
  auto created = arcane::editor::ProjectSession::create(dir, "test", registry);
  ARCANE_ASSERT_TRUE(created.has_value());
  const auto path = dir / "project.json";
  const std::string original = R"({"format_version":1,"name":"test","main_scene":"scenes/main.scene.json","future_setting":42})";
  {
    std::ofstream out(path);
    out << original;
  }
  auto opened = arcane::editor::ProjectSession::open(dir, registry);
  ARCANE_ASSERT_TRUE(opened.has_value());
  opened->mark_dirty();
  auto saved = opened->save(registry);
  ARCANE_ASSERT_TRUE(!saved.has_value());
  ARCANE_ASSERT_TRUE(saved.error().find("future_setting") != std::string::npos);
  std::ifstream in(path);
  const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  ARCANE_ASSERT_TRUE(contents == original);
  ARCANE_ASSERT_TRUE(opened->dirty());
  std::filesystem::remove_all(dir);
}

void test_save_rejects_external_edits() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_external_edits";
  std::filesystem::remove_all(dir);
  auto created = arcane::editor::ProjectSession::create(dir, "test", registry);
  ARCANE_ASSERT_TRUE(created.has_value());
  auto opened = arcane::editor::ProjectSession::open(dir, registry);
  ARCANE_ASSERT_TRUE(opened.has_value());
  const auto scene_path = opened->main_scene_path();
  const auto project_path = dir / "project.json";
  { std::ofstream out(scene_path, std::ios::app); out << " "; }
  opened->mark_dirty();
  ARCANE_ASSERT_TRUE(!opened->save(registry).has_value());
  ARCANE_ASSERT_TRUE(opened->dirty());
  auto reopened = arcane::editor::ProjectSession::open(dir, registry);
  ARCANE_ASSERT_TRUE(reopened.has_value());
  { std::ofstream out(project_path, std::ios::app); out << " "; }
  ARCANE_ASSERT_TRUE(!reopened->save(registry).has_value());
  std::ifstream in(project_path);
  const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  ARCANE_ASSERT_TRUE(!contents.empty() && contents.back() == ' ');
  std::filesystem::remove_all(dir);
}

void test_save_is_all_or_nothing() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_save_rollback";
  std::filesystem::remove_all(dir);
  ARCANE_ASSERT_TRUE(arcane::editor::ProjectSession::create(dir, "test", registry).has_value());
  auto opened = arcane::editor::ProjectSession::open(dir, registry);
  ARCANE_ASSERT_TRUE(opened.has_value());
  auto slurp = [](const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };
  const auto project_path = dir / "project.json";
  const auto scene_path = opened->main_scene_path();
  const std::string project_before = slurp(project_path), scene_before = slurp(scene_path);
  opened->mutable_project().name = "renamed";
  opened->scene().create_node("Extra");
  opened->mark_dirty();
  opened->set_fail_commit_index_for_testing(1);
  ARCANE_ASSERT_TRUE(!opened->save(registry).has_value());
  ARCANE_ASSERT_TRUE(slurp(project_path) == project_before && slurp(scene_path) == scene_before);
  ARCANE_ASSERT_TRUE(opened->dirty());
  ARCANE_ASSERT_TRUE(!std::filesystem::exists(dir / "project.json.arcane-tmp"));
  opened->set_fail_commit_index_for_testing(-1);
  ARCANE_ASSERT_TRUE(opened->save(registry).has_value());
  ARCANE_ASSERT_TRUE(slurp(project_path) != project_before && slurp(scene_path) != scene_before);
  ARCANE_ASSERT_TRUE(!opened->dirty());
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  test_registry_walk_covers_builtin_and_module_types();
  test_project_session_save_reopen_keeps_edits();
  test_asset_catalog_ids_are_stable_and_listable();
  test_strict_runtime_rejects_unknown_legacy_module_property();
  test_compatible_project_unknown_fields_cannot_be_saved();
  test_save_rejects_external_edits();
  test_save_is_all_or_nothing();
  return 0;
}
