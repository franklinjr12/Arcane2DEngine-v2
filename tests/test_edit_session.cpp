// Headless tests for the editor's command-backed edit path (AF5B): the GUI
// helper (EditSession) must give the same scene as the CLI/registry calls,
// node and property edits undo/redo exactly, drags coalesce into one step,
// the save point tracks dirty state, module properties work, and the editor
// source has no direct scene writes left on the routed paths (lint).
#include <filesystem>
#include <fstream>
#include <sstream>

#include "arcane/audio.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/module.hpp"
#include "arcane/physics.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "arcane_tooling/edit_session.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane::tooling;
namespace fs = std::filesystem;

struct TempProject {
  fs::path dir;
  explicit TempProject(const char* name) : dir(fs::temp_directory_path() / name) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
    ARCANE_ASSERT_TRUE(module.has_value());
    arcane::ComponentRegistry registry;
    arcane::register_builtin_components(registry);
    arcane::register_audio_components(registry);
    arcane::register_physics_components(registry);
    module->api()->register_types(registry);
    auto session = arcane::editor::ProjectSession::create(dir, "EditSession", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    arcane::Node* node = session->scene().create_node("Player");
    session->scene().add_component(node, "Spinner", registry);
    session->scene().add_component(node, "Sprite", registry);
    ARCANE_ASSERT_TRUE(session->save(registry).has_value());
  }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

// Asserts success and prints the command error otherwise.
template <class T>
bool ok(const std::expected<T, ToolError>& result) {
  if (!result) std::fprintf(stderr, "command failed: %s: %s\n", result.error().code.c_str(), result.error().message.c_str());
  return result.has_value();
}

std::unique_ptr<ProjectContext> open(const TempProject& project) {
  auto context = ProjectContext::open(project.dir, ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(context.has_value());
  return std::move(*context);
}

std::string scene_text(ProjectContext& context) {
  auto text = context.scene().save_canonical_json(context.registry());
  ARCANE_ASSERT_TRUE(text.has_value());
  // The id counter only grows (ids are never reused), so an undone add leaves it higher by design.
  Json doc = Json::parse(*text);
  doc.erase("next_id");
  return doc.dump(2);
}

const arcane::PropertyDescriptor& descriptor(ProjectContext& context, const char* type, const char* name) {
  for (const auto& d : *context.registry().properties_of(type)) {
    if (d.name == name) return d;
  }
  ARCANE_ASSERT_TRUE(false);
  std::exit(1);
}

arcane::ComponentId component_id(ProjectContext& context, const char* type) {
  return context.scene().find_node(1)->find_component(type)->id();
}

void test_gui_edit_matches_cli_model() {
  TempProject project("arcane_edit_session_parity");
  CommandRegistry registry;
  register_all_commands(registry);
  auto gui = open(project);
  auto cli = open(project);
  EditSession edits(*gui, registry);

  ARCANE_ASSERT_TRUE(ok(edits.set_property(1, component_id(*gui, "Sprite"), descriptor(*gui, "Sprite", "width"), 48.0f)));
  auto child = edits.run({"node.add", {{"name", "Child"}, {"parent", "id:1"}}});
  ARCANE_ASSERT_TRUE(ok(child));
  const arcane::NodeId id = child->value["id"].get<arcane::NodeId>();
  const std::string sel = "id:" + std::to_string(id);
  ARCANE_ASSERT_TRUE(ok(edits.run({"node.rename", {{"node", sel}, {"name", "Renamed"}}})));
  ARCANE_ASSERT_TRUE(ok(edits.set_transform(id, arcane::Transform2D{{3.0f, 4.0f}, 0.5f, {2.0f, 2.0f}})));

  for (const Invocation& call : std::vector<Invocation>{
           {"prop.set", {{"node", "id:1"}, {"component", "Sprite"}, {"name", "width"}, {"value", 48.0}}},
           {"node.add", {{"name", "Child"}, {"parent", "id:1"}}},
           {"node.rename", {{"node", sel}, {"name", "Renamed"}}},
           {"node.set_transform",
            {{"node", sel}, {"position", {3.0, 4.0}}, {"rotation_radians", 0.5}, {"scale", {2.0, 2.0}}}}}) {
    ARCANE_ASSERT_TRUE(ok(registry.execute(*cli, call)));
  }
  ARCANE_ASSERT_TRUE(scene_text(*gui) == scene_text(*cli));
}

void test_undo_redo_structure_and_properties() {
  TempProject project("arcane_edit_session_undo");
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = open(project);
  EditSession edits(*context, registry);
  const std::string original = scene_text(*context);

  auto added = edits.run({"node.add", {{"name", "A"}}});
  ARCANE_ASSERT_TRUE(ok(added));
  const std::string sel = "id:" + std::to_string(added->value["id"].get<arcane::NodeId>());
  const std::string after_add = scene_text(*context);
  ARCANE_ASSERT_TRUE(ok(edits.run({"node.reparent", {{"node", sel}, {"parent", "id:1"}}})));
  const std::string after_reparent = scene_text(*context);
  ARCANE_ASSERT_TRUE(ok(edits.run({"component.add", {{"node", sel}, {"type", "Sprite"}}})));
  const std::string after_component = scene_text(*context);
  ARCANE_ASSERT_TRUE(ok(edits.run({"node.remove", {{"node", sel}}})));
  ARCANE_ASSERT_TRUE(context->scene().find_node(added->value["id"].get<arcane::NodeId>()) == nullptr);

  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(scene_text(*context) == after_component);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(scene_text(*context) == after_reparent);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(scene_text(*context) == after_add);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(scene_text(*context) == original);
  ARCANE_ASSERT_TRUE(!edits.can_undo());
  ARCANE_ASSERT_TRUE(ok(edits.redo()));
  ARCANE_ASSERT_TRUE(scene_text(*context) == after_add);
}

void test_drag_coalescing() {
  TempProject project("arcane_edit_session_drag");
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = open(project);
  EditSession edits(*context, registry);
  const std::string original = scene_text(*context);
  const auto& speed = descriptor(*context, "Spinner", "speed_degrees_per_second");
  const auto spinner = component_id(*context, "Spinner");

  for (float v = 100.0f; v <= 140.0f; v += 10.0f) {
    ARCANE_ASSERT_TRUE(ok(edits.set_property(1, spinner, speed, v)));  // one drag: five frames
  }
  edits.end_gesture();
  ARCANE_ASSERT_TRUE(ok(edits.set_property(1, spinner, speed, 200.0f)));  // a second drag
  const std::string after = scene_text(*context);

  ARCANE_ASSERT_TRUE(ok(edits.undo()));  // second drag
  ARCANE_ASSERT_TRUE(std::get<float>(speed.get(*context->scene().find_node(1)->find_component("Spinner"))) == 140.0f);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));  // whole first drag in one step
  ARCANE_ASSERT_TRUE(scene_text(*context) == original);
  ARCANE_ASSERT_TRUE(!edits.can_undo());
  ARCANE_ASSERT_TRUE(ok(edits.redo()));
  ARCANE_ASSERT_TRUE(ok(edits.redo()));
  ARCANE_ASSERT_TRUE(scene_text(*context) == after);  // redo replays the final value of each drag
}

void test_save_point_dirty_tracking() {
  TempProject project("arcane_edit_session_save_point");
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = open(project);
  EditSession edits(*context, registry);
  ARCANE_ASSERT_TRUE(!edits.modified());

  ARCANE_ASSERT_TRUE(ok(edits.run({"node.rename", {{"node", "id:1"}, {"name", "One"}}})));
  ARCANE_ASSERT_TRUE(edits.modified());
  edits.mark_saved();
  ARCANE_ASSERT_TRUE(!edits.modified());
  ARCANE_ASSERT_TRUE(ok(edits.run({"node.rename", {{"node", "id:1"}, {"name", "Two"}}})));
  ARCANE_ASSERT_TRUE(edits.modified());
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(!edits.modified());  // back at the save point
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(edits.modified());
  ARCANE_ASSERT_TRUE(ok(edits.redo()));
  ARCANE_ASSERT_TRUE(!edits.modified());

  // Typing right after saving must not merge into the saved step.
  ARCANE_ASSERT_TRUE(ok(edits.run({"node.rename", {{"node", "id:1"}, {"name", "T"}}}, "rename:1")));
  edits.mark_saved();
  ARCANE_ASSERT_TRUE(ok(edits.run({"node.rename", {{"node", "id:1"}, {"name", "Tw"}}}, "rename:1")));
  ARCANE_ASSERT_TRUE(edits.modified());
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(!edits.modified());
  ARCANE_ASSERT_TRUE(context->scene().find_node(1)->name() == "T");

  // Branching away from an undone save point makes it unreachable.
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(ok(edits.run({"node.rename", {{"node", "id:1"}, {"name", "Other"}}})));
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(edits.modified());
}

void test_failed_edit_changes_nothing() {
  TempProject project("arcane_edit_session_fail");
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = open(project);
  EditSession edits(*context, registry);
  const std::string original = scene_text(*context);
  ARCANE_ASSERT_TRUE(!edits.run({"node.rename", {{"node", "id:1"}, {"name", "a/b"}}}));
  ARCANE_ASSERT_TRUE(!edits.run({"node.reparent", {{"node", "id:1"}, {"parent", "id:1"}}}));
  ARCANE_ASSERT_TRUE(scene_text(*context) == original);
  ARCANE_ASSERT_TRUE(!edits.can_undo() && !edits.modified());
}

// Routed editor paths must not write the scene directly any more (AF5B). Tile painting,
// clips, HUD and input maps are AF6/AF7 and listed in docs/editor-command-inventory.md.
void test_editor_source_has_no_direct_scene_writes() {
  std::ifstream in(ARCANE_EDITOR_SOURCE_PATH);
  ARCANE_ASSERT_TRUE(in.good());
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string source = ss.str();
  for (const char* forbidden : {"->set_name(", "create_node(", "destroy_node(", "flush_destroyed(", "->reparent(",
                                 "add_component(", "descriptor.set(", "local_transform() =", "auto& transform = ", "sprite->texture",
                                 "->texture =", "->clip =", "->tileset ="}) {
    if (source.find(forbidden) != std::string::npos) {
      std::fprintf(stderr, "editor_main.cpp writes the scene directly: %s\n", forbidden);
      ARCANE_ASSERT_TRUE(false);
    }
  }
}

}  // namespace

int main() {
  test_gui_edit_matches_cli_model();
  test_undo_redo_structure_and_properties();
  test_drag_coalescing();
  test_save_point_dirty_tracking();
  test_failed_edit_changes_nothing();
  test_editor_source_has_no_direct_scene_writes();
  return 0;
}
