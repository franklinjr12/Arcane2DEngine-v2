#include <optional>

#include "arcane/component_registry.hpp"
#include "arcane/module.hpp"
#include "arcane/node.hpp"
#include "arcane/property.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

const arcane::PropertyDescriptor* find_property(const arcane::ComponentRegistry& registry,
                                                const std::string& name) {
  for (const auto& p : *registry.properties_of("Spinner")) {
    if (p.name == name) {
      return &p;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  auto good = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(good.has_value());
  ARCANE_ASSERT_TRUE(good->api() != nullptr);
  ARCANE_ASSERT_TRUE(good->api()->api_version == arcane::kGameApiVersion);

  auto bad = arcane::GameModule::load(ARCANE_BAD_VERSION_MODULE_PATH);
  ARCANE_ASSERT_TRUE(!bad.has_value());

  auto missing = arcane::GameModule::load("/nonexistent/path/not_a_module.so");
  ARCANE_ASSERT_TRUE(!missing.has_value());

  // Refuse unload while registry entries and component instances are alive.
  {
    auto loaded = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
    ARCANE_ASSERT_TRUE(loaded.has_value());
    arcane::GameModule& module = *loaded;
    ARCANE_ASSERT_TRUE(module.live_references() == 0);

    std::optional<arcane::ComponentRegistry> registry;
    registry.emplace();
    module.register_types(*registry);
    const long with_registry = module.live_references();
    ARCANE_ASSERT_TRUE(with_registry > 0);

    std::optional<arcane::Scene> scene;
    scene.emplace();
    arcane::Node* node = scene->create_node("spinner_node");
    arcane::Component* component = scene->add_component(node, "Spinner", *registry);
    ARCANE_ASSERT_TRUE(component != nullptr);
    ARCANE_ASSERT_TRUE(module.live_references() > with_registry);

    auto refused = module.unload();
    ARCANE_ASSERT_TRUE(!refused.has_value());
    ARCANE_ASSERT_TRUE(module.api() != nullptr);

    const arcane::PropertyDescriptor* speed = find_property(*registry, "speed_degrees_per_second");
    ARCANE_ASSERT_TRUE(speed != nullptr);
    speed->set(*component, arcane::PropertyValue{42.0f});
    ARCANE_ASSERT_TRUE(std::get<float>(speed->get(*component)) == 42.0f);

    scene.reset();
    registry.reset();
    ARCANE_ASSERT_TRUE(module.live_references() == 0);
    ARCANE_ASSERT_TRUE(module.unload().has_value());
    ARCANE_ASSERT_TRUE(module.api() == nullptr);
    ARCANE_ASSERT_TRUE(module.unload().has_value());
  }

  // A registry that outlives the module keeps the library mapped.
  {
    arcane::ComponentRegistry registry;
    {
      auto loaded = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
      ARCANE_ASSERT_TRUE(loaded.has_value());
      loaded->register_types(registry);
      ARCANE_ASSERT_TRUE(loaded->live_references() > 0);
    }  // module destroyed here: logs an error and skips dlclose
    ARCANE_ASSERT_TRUE(registry.contains("Spinner"));
    std::unique_ptr<arcane::Component> spinner = registry.create("Spinner");
    ARCANE_ASSERT_TRUE(spinner != nullptr);
    const arcane::PropertyDescriptor* speed = find_property(registry, "speed_degrees_per_second");
    ARCANE_ASSERT_TRUE(speed != nullptr);
    speed->set(*spinner, arcane::PropertyValue{7.0f});
    ARCANE_ASSERT_TRUE(std::get<float>(speed->get(*spinner)) == 7.0f);
  }

  // Builtin (unowned) registrations are unaffected by module ownership.
  {
    struct Plain : arcane::Component {};
    arcane::ComponentRegistry registry;
    registry.register_component("Plain", [] { return std::make_unique<Plain>(); }, {});
    auto loaded = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
    ARCANE_ASSERT_TRUE(loaded.has_value());
    ARCANE_ASSERT_TRUE(registry.create("Plain") != nullptr);
    ARCANE_ASSERT_TRUE(loaded->live_references() == 0);
    ARCANE_ASSERT_TRUE(loaded->unload().has_value());
  }

  return 0;
}
