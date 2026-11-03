// Registry introspection and round trip: lists every registered component
// type (built-ins + the sample_game module), sets non-default values by name,
// saves canonical JSON, reloads, and compares values and bytes.
#include <algorithm>
#include <filesystem>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

// A value of the right type that differs from `def`. Entity references point
// at `entity` (a node that exists in the test scene) so loading keeps them.
PropertyValue non_default(const std::string& type_name, const PropertyInfo& p, EntityId entity) {
  // Tilemap layers are parsed text (";" between layers, "," between ids).
  if (type_name == "Tilemap" && p.name == "layers") return std::string("1,2;3,4");
  switch (p.type) {
    case PropertyType::Bool:
      return !std::get<bool>(p.default_value);
    case PropertyType::Int:
      return std::get<int>(p.default_value) == 7 ? 9 : 7;
    case PropertyType::Float:
      return std::get<float>(p.default_value) + 1.25f;
    case PropertyType::Vec2: {
      Vec2 v = std::get<Vec2>(p.default_value);
      return Vec2{v.x + 1.5f, v.y - 2.5f};
    }
    case PropertyType::Color: {
      Color c = std::get<Color>(p.default_value);
      return Color{c.r * 0.5f, c.g * 0.25f, c.b * 0.75f, c.a * 0.5f};
    }
    case PropertyType::String:
      return std::string("v\"\\\xC3\xA9\n") + std::get<std::string>(p.default_value);
    case PropertyType::AssetId:
      return std::get<AssetId>(p.default_value) + 42;
    case PropertyType::EntityId:
      return entity;
  }
  return p.default_value;
}

// A value of a different type than `p` expects.
PropertyValue wrong_type(const PropertyInfo& p) {
  return p.type == PropertyType::String ? PropertyValue{true} : PropertyValue{std::string("nope")};
}

void check_introspection(const ComponentRegistry& registry) {
  const auto infos = describe_components(registry);
  ARCANE_ASSERT_TRUE(infos.size() >= 4);
  ARCANE_ASSERT_TRUE(std::is_sorted(infos.begin(), infos.end(),
                                    [](const ComponentInfo& a, const ComponentInfo& b) { return a.type_name < b.type_name; }));
  ARCANE_ASSERT_TRUE(describe_components(registry).size() == infos.size());
  bool saw_sprite = false;
  bool saw_module_type = false;
  for (const auto& info : infos) {
    saw_sprite |= info.type_name == "Sprite";
    saw_module_type |= info.type_name == "Spinner";
    const auto* descriptors = registry.properties_of(info.type_name);
    ARCANE_ASSERT_TRUE(descriptors != nullptr && descriptors->size() == info.properties.size());
    for (std::size_t i = 0; i < info.properties.size(); ++i) {
      ARCANE_ASSERT_TRUE(info.properties[i].name == (*descriptors)[i].name);
      ARCANE_ASSERT_TRUE(value_fits_type(info.properties[i].type, info.properties[i].default_value));
    }
  }
  ARCANE_ASSERT_TRUE(saw_sprite);
  ARCANE_ASSERT_TRUE(saw_module_type);
}

void check_rejections(const ComponentRegistry& registry) {
  for (const auto& info : describe_components(registry)) {
    auto comp = registry.create(info.type_name);
    ARCANE_ASSERT_TRUE(comp != nullptr);
    ARCANE_ASSERT_TRUE(!set_property_by_name(registry, info.type_name, *comp, "no_such_property", true).has_value());
    ARCANE_ASSERT_TRUE(!get_property_by_name(registry, info.type_name, *comp, "no_such_property").has_value());
    for (const auto& p : info.properties) {
      ARCANE_ASSERT_TRUE(!set_property_by_name(registry, info.type_name, *comp, p.name, wrong_type(p)).has_value());
      auto after = get_property_by_name(registry, info.type_name, *comp, p.name);
      ARCANE_ASSERT_TRUE(after.has_value() && *after == p.default_value);  // data unchanged
    }
  }
  auto comp = registry.create("Sprite");
  ARCANE_ASSERT_TRUE(!set_property_by_name(registry, "NoSuchType", *comp, "width", 1.0f).has_value());
}

void check_round_trip(const ComponentRegistry& registry) {
  const auto infos = describe_components(registry);
  Scene scene;
  Node* anchor = scene.create_node("anchor");
  for (const auto& info : infos) {
    Node* node = scene.create_node(info.type_name);
    Component* comp = scene.add_component(node, info.type_name, registry);
    ARCANE_ASSERT_TRUE(comp != nullptr);
    for (const auto& p : info.properties) {
      auto set = set_property_by_name(registry, info.type_name, *comp, p.name, non_default(info.type_name, p, anchor->id()));
      ARCANE_ASSERT_TRUE(set.has_value());
    }
  }

  auto first = scene.save_canonical_json(registry);
  ARCANE_ASSERT_TRUE(first.has_value());
  ARCANE_ASSERT_TRUE(scene.save_canonical_json(registry).value() == *first);  // save twice = identical

  auto loaded = Scene::load_from_json(*first, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->save_canonical_json(registry).value() == *first);  // reload then save = identical

  for (const auto& info : infos) {
    const Node* a = scene.find_node_by_name(info.type_name);
    const Node* b = loaded->find_node_by_name(info.type_name);
    ARCANE_ASSERT_TRUE(a != nullptr && b != nullptr);
    ARCANE_ASSERT_TRUE(a->components().size() == 1 && b->components().size() == 1);
    for (const auto& p : info.properties) {
      auto va = get_property_by_name(registry, info.type_name, *a->components()[0], p.name);
      auto vb = get_property_by_name(registry, info.type_name, *b->components()[0], p.name);
      ARCANE_ASSERT_TRUE(va.has_value() && vb.has_value());
      if (*va != *vb) std::fprintf(stderr, "mismatch %s.%s\n", info.type_name.c_str(), p.name.c_str());
      ARCANE_ASSERT_TRUE(*va == *vb);
      ARCANE_ASSERT_TRUE(*vb != p.default_value);  // the non-default value really survived
    }
  }
}

}  // namespace

int main() {
  auto runtime = arcane::editor::ProjectRuntime::open(ARCANE_SAMPLE_PROJECT_DIR, ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(runtime.has_value());
  check_introspection(runtime->registry());
  check_rejections(runtime->registry());
  check_round_trip(runtime->registry());
  return 0;
}
