// collect_render_order: layers, order, y-sort and tie-breaks (no GL needed).
#include <string>
#include <vector>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/scene_render.hpp"
#include "test_assert.hpp"

namespace {

struct Fixture {
  arcane::ComponentRegistry registry;
  arcane::Scene scene;
  Fixture() { arcane::register_builtin_components(registry); }

  arcane::Node* node(const std::string& name, arcane::Node* parent = nullptr) { return scene.create_node(name, parent); }
  arcane::RenderOrder* order(arcane::Node* n, int layer, int order = 0, bool y_sort = false, float offset = 0.0f) {
    auto* ro = static_cast<arcane::RenderOrder*>(scene.add_component(n, "RenderOrder", registry));
    ro->layer = layer;
    ro->order = order;
    ro->y_sort = y_sort;
    ro->y_sort_offset = offset;
    return ro;
  }
  std::string names() {
    std::string out;
    for (arcane::Node* n : arcane::editor::collect_render_order(scene)) out += n->name() + ",";
    return out;
  }
};

void test_default_is_preorder() {
  Fixture f;
  auto* a = f.node("a");
  f.node("a1", a);
  f.node("b");
  ARCANE_ASSERT_TRUE(f.names() == "a,a1,b,");
}

void test_layer_and_order() {
  Fixture f;
  f.order(f.node("hi"), 2);
  f.node("mid");
  f.order(f.node("low"), -1);
  ARCANE_ASSERT_TRUE(f.names() == "low,mid,hi,");
  Fixture g;
  g.order(g.node("a"), 1, 5);
  g.order(g.node("b"), 1, 2);
  g.order(g.node("c"), 1, 2);
  g.order(g.node("d"), 0, 9);
  ARCANE_ASSERT_TRUE(g.names() == "d,b,c,a,");
}

void test_y_sort() {
  Fixture f;
  auto* low = f.node("low");
  low->local_transform().position = {0.0f, 50.0f};
  f.order(low, 0, 0, true);
  auto* high = f.node("high");
  high->local_transform().position = {0.0f, 10.0f};
  f.order(high, 0, 0, true);
  auto* plain = f.node("plain");
  plain->local_transform().position = {0.0f, 100.0f};
  auto* offset = f.node("offset");  // y 0 + 30 sorts between high and low
  f.order(offset, 0, 0, true, 30.0f);
  f.node("plain2");
  ARCANE_ASSERT_TRUE(f.names() == "plain,plain2,high,offset,low,");
}

void test_ties_keep_preorder() {
  Fixture f;
  for (const char* name : {"a", "b", "c"}) f.order(f.node(name), 0, 0, true);
  ARCANE_ASSERT_TRUE(f.names() == "a,b,c,");
}

void test_inactive_excluded() {
  Fixture f;
  auto* off = f.node("off");
  f.node("child", off);
  f.node("on");
  off->set_active(false);
  ARCANE_ASSERT_TRUE(f.names() == "on,");
}

void test_round_trip() {
  Fixture f;
  f.order(f.node("n"), 3, -2, true, 4.5f);
  auto text = f.scene.save_canonical_json(f.registry);
  ARCANE_ASSERT_TRUE(text.has_value());
  auto loaded = arcane::Scene::load_from_json(*text, f.registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  const auto* ro = static_cast<arcane::RenderOrder*>(loaded->roots()[0]->find_component("RenderOrder"));
  ARCANE_ASSERT_TRUE(ro && ro->layer == 3 && ro->order == -2 && ro->y_sort && ro->y_sort_offset == 4.5f);
}

void test_post_process_order() {
  Fixture f;
  const auto pass = [&](const char* name, int order, bool enabled, arcane::Node* parent = nullptr) {
    auto* p = static_cast<arcane::PostProcess*>(f.scene.add_component(f.node(name, parent), "PostProcess", f.registry));
    p->order = order;
    p->enabled = enabled;
    return p;
  };
  ARCANE_ASSERT_TRUE(arcane::editor::collect_post_process(f.scene).empty());
  auto* late = pass("late", 5, true);
  auto* off = pass("off", 0, false);
  auto* early_a = pass("early_a", 1, true);
  auto* group = f.node("group");
  auto* early_b = pass("early_b", 1, true, group);
  auto* hidden_group = f.node("hidden_group");
  pass("hidden", -3, true, hidden_group);
  hidden_group->set_active(false);
  (void)off;
  const auto chain = arcane::editor::collect_post_process(f.scene);
  ARCANE_ASSERT_TRUE(chain.size() == 3);
  ARCANE_ASSERT_TRUE(chain[0] == early_a && chain[1] == early_b && chain[2] == late);
  const auto* defaults = static_cast<arcane::PostProcess*>(f.scene.add_component(f.node("d"), "PostProcess", f.registry));
  ARCANE_ASSERT_TRUE(defaults->material == 0 && defaults->order == 0 && defaults->enabled);
}

}  // namespace

int main() {
  test_post_process_order();
  test_default_is_preorder();
  test_layer_and_order();
  test_y_sort();
  test_ties_keep_preorder();
  test_inactive_excluded();
  test_round_trip();
  return 0;
}
