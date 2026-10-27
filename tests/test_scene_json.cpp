#include <memory>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/project.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

class Spinner : public arcane::Component {
 public:
  float speed = 0.0f;
  std::string label;
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  registry.register_component(
      "Spinner", [] { return std::make_unique<Spinner>(); },
      {
          arcane::make_property<Spinner>("speed", arcane::PropertyType::Float, &Spinner::speed),
          arcane::make_property<Spinner>("label", arcane::PropertyType::String, &Spinner::label),
      });
  return registry;
}

void test_scene_round_trip() {
  arcane::ComponentRegistry registry = make_registry();

  arcane::Scene original;
  arcane::Node* root = original.create_node("root");
  root->local_transform().position = {1.0f, 2.0f};
  arcane::Node* child = original.create_node("child", root);
  child->local_transform().rotation_radians = 0.5f;
  arcane::Component* spinner = original.add_component(child, "Spinner", registry);
  static_cast<Spinner*>(spinner)->speed = 42.5f;
  static_cast<Spinner*>(spinner)->label = "fast";

  const std::string json = original.save_to_json(registry);

  auto loaded = arcane::Scene::load_from_json(json, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  arcane::Scene& reloaded = *loaded;

  ARCANE_ASSERT_TRUE(reloaded.roots().size() == 1);
  arcane::Node* reloaded_root = reloaded.roots()[0].get();
  ARCANE_ASSERT_TRUE(reloaded_root->id() == root->id());
  ARCANE_ASSERT_TRUE(reloaded_root->name() == "root");
  ARCANE_ASSERT_TRUE(reloaded_root->local_transform().position.x == 1.0f);
  ARCANE_ASSERT_TRUE(reloaded_root->children().size() == 1);

  arcane::Node* reloaded_child = reloaded_root->children()[0].get();
  ARCANE_ASSERT_TRUE(reloaded_child->id() == child->id());
  ARCANE_ASSERT_TRUE(reloaded_child->local_transform().rotation_radians == 0.5f);
  ARCANE_ASSERT_TRUE(reloaded_child->components().size() == 1);

  auto* reloaded_spinner = static_cast<Spinner*>(reloaded_child->components()[0].get());
  ARCANE_ASSERT_TRUE(reloaded_spinner->speed == 42.5f);
  ARCANE_ASSERT_TRUE(reloaded_spinner->label == "fast");

  // next_id must have survived (or been recomputed) so further allocation
  // never reissues an id used in the saved file.
  ARCANE_ASSERT_TRUE(reloaded.next_id() == original.next_id());
  arcane::Node* fresh = reloaded.create_node("fresh");
  ARCANE_ASSERT_TRUE(fresh->id() != reloaded_root->id() && fresh->id() != reloaded_child->id() &&
                      fresh->id() != reloaded_spinner->id());
}

void test_rejects_missing_format_version() {
  arcane::ComponentRegistry registry = make_registry();
  auto result = arcane::Scene::load_from_json(R"({"nodes":[]})", registry);
  ARCANE_ASSERT_TRUE(!result.has_value());
}

void test_rejects_newer_format_version() {
  arcane::ComponentRegistry registry = make_registry();
  auto result = arcane::Scene::load_from_json(R"({"format_version":999,"nodes":[]})", registry);
  ARCANE_ASSERT_TRUE(!result.has_value());
  ARCANE_ASSERT_TRUE(result.error().find("newer") != std::string::npos);
}

void test_rejects_unknown_component_type() {
  arcane::ComponentRegistry registry = make_registry();
  const char* json = R"({
    "format_version": 1,
    "next_id": 2,
    "nodes": [
      {"id": 1, "name": "root", "parent_id": 0,
       "components": [{"id": 0, "type": "DoesNotExist", "properties": {}}]}
    ]
  })";
  auto result = arcane::Scene::load_from_json(json, registry);
  ARCANE_ASSERT_TRUE(!result.has_value());
  ARCANE_ASSERT_TRUE(result.error().find("DoesNotExist") != std::string::npos);
}

void test_project_round_trip() {
  arcane::ProjectFile project;
  project.name = "sample_project";
  project.main_scene = "scenes/main.scene.json";
  project.input_actions.add_binding("jump", arcane::InputBinding{arcane::InputDevice::Keyboard, arcane::Keys::kSpace});

  const std::string json = arcane::save_project_to_json(project);
  auto loaded = arcane::load_project_from_json(json);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->name == "sample_project");
  ARCANE_ASSERT_TRUE(loaded->main_scene == "scenes/main.scene.json");
  ARCANE_ASSERT_TRUE(loaded->input_actions.actions().size() == 1);
  ARCANE_ASSERT_TRUE(loaded->input_actions.actions()[0].name == "jump");

  auto bad_version = arcane::load_project_from_json(R"({"format_version":2,"name":"x","main_scene":"y"})");
  ARCANE_ASSERT_TRUE(!bad_version.has_value());
}

}  // namespace

int main() {
  test_scene_round_trip();
  test_rejects_missing_format_version();
  test_rejects_newer_format_version();
  test_rejects_unknown_component_type();
  test_project_round_trip();
  return 0;
}
