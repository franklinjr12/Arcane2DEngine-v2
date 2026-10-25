#include <memory>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/editor_state.hpp"
#include "test_assert.hpp"

namespace {

class Counter : public arcane::Component {
 public:
  int value = 0;
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  registry.register_component(
      "Counter", [] { return std::make_unique<Counter>(); },
      {arcane::make_property<Counter>("value", arcane::PropertyType::Int, &Counter::value)});
  return registry;
}

// Core invariant from docs/decisions.md: Play snapshots the authored scene
// into a temporary runtime scene; mutations during Play must not leak back
// into the authored scene after Stop.
void test_play_stop_isolation() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::Scene authored;
  arcane::Node* node = authored.create_node("root");
  auto* counter = static_cast<Counter*>(authored.add_component(node, "Counter", registry));
  counter->value = 1;

  arcane::editor::PlayStopController controller;
  ARCANE_ASSERT_TRUE(controller.mode() == arcane::editor::EditorMode::Edit);
  ARCANE_ASSERT_TRUE(controller.play(authored, registry));
  ARCANE_ASSERT_TRUE(controller.mode() == arcane::editor::EditorMode::Play);

  arcane::Scene* runtime = controller.active_scene(authored);
  ARCANE_ASSERT_TRUE(runtime != &authored);
  auto* runtime_counter = static_cast<Counter*>(runtime->roots()[0]->components()[0].get());
  runtime_counter->value = 99;

  controller.stop();
  ARCANE_ASSERT_TRUE(controller.mode() == arcane::editor::EditorMode::Edit);
  ARCANE_ASSERT_TRUE(controller.active_scene(authored) == &authored);
  ARCANE_ASSERT_TRUE(counter->value == 1);
}

void test_selection() {
  arcane::editor::Selection selection;
  ARCANE_ASSERT_TRUE(!selection.has_selection());
  selection.select(arcane::NodeId{7});
  ARCANE_ASSERT_TRUE(selection.has_selection());
  ARCANE_ASSERT_TRUE(selection.selected().value() == arcane::NodeId{7});
  selection.clear();
  ARCANE_ASSERT_TRUE(!selection.has_selection());
}

}  // namespace

int main() {
  test_play_stop_isolation();
  test_selection();
  return 0;
}
