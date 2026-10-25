// Exercises the module registration ABI end to end: load sample_game,
// register its component types, create one by name, set/get a property
// through the registered descriptor, then save and reload the scene.
#include "arcane/component_registry.hpp"
#include "arcane/module.hpp"
#include "arcane/node.hpp"
#include "arcane/property.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

int main() {
  auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(module.has_value());
  ARCANE_ASSERT_TRUE(module->api()->register_types != nullptr);

  arcane::ComponentRegistry registry;
  module->api()->register_types(registry);
  ARCANE_ASSERT_TRUE(registry.contains("Spinner"));

  const std::vector<arcane::PropertyDescriptor>* properties = registry.properties_of("Spinner");
  ARCANE_ASSERT_TRUE(properties != nullptr);
  ARCANE_ASSERT_TRUE(properties->size() >= 2);

  arcane::Scene scene;
  arcane::Node* node = scene.create_node("spinner_node");
  arcane::Component* component = scene.add_component(node, "Spinner", registry);
  ARCANE_ASSERT_TRUE(component != nullptr);

  const arcane::PropertyDescriptor* speed_property = nullptr;
  for (const auto& p : *properties) {
    if (p.name == "speed_degrees_per_second") {
      speed_property = &p;
    }
  }
  ARCANE_ASSERT_TRUE(speed_property != nullptr);
  speed_property->set(*component, arcane::PropertyValue{180.0f});
  ARCANE_ASSERT_TRUE(std::get<float>(speed_property->get(*component)) == 180.0f);

  const std::string json = scene.save_to_json(registry);
  auto reloaded = arcane::Scene::load_from_json(json, registry);
  ARCANE_ASSERT_TRUE(reloaded.has_value());
  arcane::Node* reloaded_node = reloaded->find_node(node->id());
  ARCANE_ASSERT_TRUE(reloaded_node != nullptr);
  arcane::Component* reloaded_component = reloaded_node->find_component("Spinner");
  ARCANE_ASSERT_TRUE(reloaded_component != nullptr);
  ARCANE_ASSERT_TRUE(std::get<float>(speed_property->get(*reloaded_component)) == 180.0f);

  return 0;
}
