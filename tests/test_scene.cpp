#include <cmath>
#include <memory>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

std::vector<std::string>& event_log() {
  static std::vector<std::string> log;
  return log;
}

class Recorder : public arcane::Component {
 public:
  void on_create() override { event_log().push_back("create:" + node()->name()); }
  void on_start() override { event_log().push_back("start:" + node()->name()); }
  void on_update(float) override { event_log().push_back("update:" + node()->name()); }
  void on_destroy() override { event_log().push_back("destroy:" + node()->name()); }
};

void test_ids_and_lookup() {
  arcane::Scene scene;
  arcane::Node* a = scene.create_node("a");
  arcane::Node* b = scene.create_node("b");
  ARCANE_ASSERT_TRUE(a->id() != arcane::kInvalidNodeId);
  ARCANE_ASSERT_TRUE(b->id() != arcane::kInvalidNodeId);
  ARCANE_ASSERT_TRUE(a->id() != b->id());
  ARCANE_ASSERT_TRUE(scene.find_node(a->id()) == a);
  ARCANE_ASSERT_TRUE(scene.find_node(b->id()) == b);
  ARCANE_ASSERT_TRUE(scene.find_node(arcane::kInvalidNodeId) == nullptr);
}

void test_ids_not_reused_after_destroy() {
  arcane::Scene scene;
  arcane::Node* a = scene.create_node("a");
  const arcane::NodeId a_id = a->id();
  scene.destroy_node(a_id);
  scene.flush_destroyed();
  ARCANE_ASSERT_TRUE(scene.find_node(a_id) == nullptr);
  arcane::Node* c = scene.create_node("c");
  ARCANE_ASSERT_TRUE(c->id() != a_id);
}

void test_reparent_and_cycle_rejection() {
  arcane::Scene scene;
  arcane::Node* root = scene.create_node("root");
  arcane::Node* child = scene.create_node("child", root);
  arcane::Node* grandchild = scene.create_node("grandchild", child);
  arcane::Node* other = scene.create_node("other");

  ARCANE_ASSERT_TRUE(scene.reparent(grandchild, other));
  ARCANE_ASSERT_TRUE(grandchild->parent() == other);

  // A node can't become its own parent, nor move under its own descendant.
  ARCANE_ASSERT_TRUE(!scene.reparent(child, child));
  ARCANE_ASSERT_TRUE(!scene.reparent(root, child));
  ARCANE_ASSERT_TRUE(root->parent() == nullptr);
}

void test_destroy_subtree() {
  arcane::Scene scene;
  arcane::Node* root = scene.create_node("root");
  arcane::Node* child = scene.create_node("child", root);
  arcane::Node* grandchild = scene.create_node("grandchild", child);
  const arcane::NodeId child_id = child->id();
  const arcane::NodeId grandchild_id = grandchild->id();

  scene.destroy_node(child_id);
  // Lookups are removed immediately, even though the nodes are only
  // physically unlinked at the next flush.
  ARCANE_ASSERT_TRUE(scene.find_node(child_id) == nullptr);
  ARCANE_ASSERT_TRUE(scene.find_node(grandchild_id) == nullptr);
  ARCANE_ASSERT_TRUE(root->children().size() == 1);
  scene.flush_destroyed();
  ARCANE_ASSERT_TRUE(root->children().empty());
}

void test_lifecycle_order() {
  event_log().clear();
  arcane::ComponentRegistry registry;
  registry.register_component("Recorder", [] { return std::make_unique<Recorder>(); }, {});

  arcane::Scene scene;
  arcane::Node* node = scene.create_node("n");
  scene.add_component(node, "Recorder", registry);
  ARCANE_ASSERT_TRUE(event_log() == std::vector<std::string>{"create:n"});

  scene.update(1.0f / 60.0f);
  scene.update(1.0f / 60.0f);
  ARCANE_ASSERT_TRUE(
      (event_log() == std::vector<std::string>{"create:n", "start:n", "update:n", "update:n"}));

  scene.destroy_node(node->id());
  scene.update(1.0f / 60.0f);  // destroy happens during this tick's flush, not before
  ARCANE_ASSERT_TRUE((event_log() ==
                       std::vector<std::string>{"create:n", "start:n", "update:n", "update:n", "destroy:n"}));
}

void test_transform_composition() {
  arcane::Scene scene;
  arcane::Node* parent = scene.create_node("parent");
  parent->local_transform().position = {10.0f, 0.0f};
  parent->local_transform().rotation_radians = 0.0f;
  parent->local_transform().scale = {2.0f, 2.0f};

  arcane::Node* child = scene.create_node("child", parent);
  child->local_transform().position = {1.0f, 0.0f};

  const arcane::WorldTransform2D world = child->world_transform();
  ARCANE_ASSERT_TRUE(std::abs(world.position.x - 12.0f) < 1e-5f);  // 10 + 1*2
  ARCANE_ASSERT_TRUE(std::abs(world.position.y - 0.0f) < 1e-5f);
  ARCANE_ASSERT_TRUE(std::abs(world.scale.x - 2.0f) < 1e-5f);
}

void test_registry() {
  arcane::ComponentRegistry registry;
  ARCANE_ASSERT_TRUE(!registry.contains("Recorder"));
  registry.register_component("Recorder", [] { return std::make_unique<Recorder>(); }, {});
  ARCANE_ASSERT_TRUE(registry.contains("Recorder"));
  ARCANE_ASSERT_TRUE(registry.create("Recorder") != nullptr);
  ARCANE_ASSERT_TRUE(registry.create("Missing") == nullptr);
  ARCANE_ASSERT_TRUE(registry.properties_of("Recorder") != nullptr);
  ARCANE_ASSERT_TRUE(registry.properties_of("Missing") == nullptr);
}

}  // namespace

int main() {
  test_ids_and_lookup();
  test_ids_not_reused_after_destroy();
  test_reparent_and_cycle_rejection();
  test_destroy_subtree();
  test_lifecycle_order();
  test_transform_composition();
  test_registry();
  return 0;
}
