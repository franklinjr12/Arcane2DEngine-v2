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

}  // namespace

int main() {
  test_registry_walk_covers_builtin_and_module_types();
  test_project_session_save_reopen_keeps_edits();
  test_asset_catalog_ids_are_stable_and_listable();
  return 0;
}
