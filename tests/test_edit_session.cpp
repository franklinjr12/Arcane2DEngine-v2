// Headless tests for the editor's command-backed edit path (AF5B): the GUI
// helper (EditSession) must give the same scene as the CLI/registry calls,
// node and property edits undo/redo exactly, drags coalesce into one step,
// the save point tracks dirty state, module properties work, HUD and input map
// edits (AF7B) match the CLI model and undo exactly, and the editor source has
// no direct scene or project writes left on the routed paths (lint). AF8B adds
// parity/undo for audio, physics, particle and module properties, module
// edits surviving a reopen, and selection/Play/Stop staying out of the history.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "arcane/audio.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/hud.hpp"
#include "arcane/module.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"
#include "arcane_editor/editor_state.hpp"
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
    arcane::register_particle_components(registry);
    module->api()->register_types(registry);
    auto session = arcane::editor::ProjectSession::create(dir, "EditSession", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    arcane::Node* node = session->scene().create_node("Player");
    session->scene().add_component(node, "Spinner", registry);
    session->scene().add_component(node, "Sprite", registry);
    for (const char* type : {"AudioSource", "RigidBody2D", "Collider2D", "ParticleEmitter"}) {
      session->scene().add_component(node, type, registry);
    }
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

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The HUD and input map calls the editor panels make, in panel order (a drag sends several hud.set).
std::vector<Invocation> hud_input_calls() {
  return {{"hud.add", {{"type", "button"}}},
          {"hud.add", {{"type", "text"}}},
          {"hud.add", {{"type", "image"}}},
          {"hud.add", {{"type", "bar"}}},
          {"hud.set", {{"index", 0}, {"field", "label"}, {"value", "Go"}}},
          {"hud.set", {{"index", 1}, {"field", "color"}, {"value", {0.5, 0.25, 1.0, 1.0}}}},
          {"hud.set", {{"index", 2}, {"field", "uv"}, {"value", {0.0, 0.0, 0.5, 0.5}}}},
          {"hud.set", {{"index", 2}, {"field", "tint"}, {"value", {1.0, 0.5, 0.5, 1.0}}}},
          {"hud.set", {{"index", 2}, {"field", "anchor"}, {"value", "BottomRight"}}},
          {"hud.set", {{"index", 2}, {"field", "width"}, {"value", 64.0}}},
          {"hud.remove", {{"index", 3}}},
          {"input.add_action", {{"name", "jump"}}},
          {"input.add_action", {{"name", "fire"}, {"player", 1}}},
          {"input.add_binding", {{"action", "jump"}, {"device", "keyboard"}, {"code", 32}}},
          {"input.add_binding", {{"action", "jump"}, {"device", "gamepad_axis"}}},
          {"input.set_binding", {{"action", "jump"}, {"index", 1}, {"field", "deadzone"}, {"value", 0.25}}},
          {"input.set_binding", {{"action", "jump"}, {"index", 1}, {"field", "axis_sign"}, {"value", -1}}},
          {"input.set_player", {{"action", "jump"}, {"player", 2}}},
          {"input.remove_binding", {{"action", "jump"}, {"index", 0}}},
          {"input.remove_action", {{"action", "fire"}}}};
}

// AF7B: the editor's HUD and Input Map panels edit project.json data in memory through EditSession; the CLI model
// persists each command. Both must write the same canonical project.json, and undo must restore the original.
void test_gui_hud_input_matches_cli_model() {
  TempProject gui_project("arcane_edit_session_hud_gui");
  TempProject cli_project("arcane_edit_session_hud_cli");
  CommandRegistry registry;
  register_all_commands(registry);
  const std::string original = read_file(gui_project.dir / "project.json");
  ARCANE_ASSERT_TRUE(original == read_file(cli_project.dir / "project.json"));
  auto gui = open(gui_project);
  auto cli = open(cli_project);
  EditSession edits(*gui, registry);

  for (const Invocation& call : hud_input_calls()) {
    ARCANE_ASSERT_TRUE(ok(edits.run(call)));
    ARCANE_ASSERT_TRUE(ok(registry.execute(*cli, call, {.persist = true})));
  }
  ARCANE_ASSERT_TRUE(edits.modified());
  ARCANE_ASSERT_TRUE(gui->project_dirty());
  ARCANE_ASSERT_TRUE(ok(gui->save()));  // the in-memory edits reach disk only on save
  ARCANE_ASSERT_TRUE(read_file(gui_project.dir / "project.json") == read_file(cli_project.dir / "project.json"));
  ARCANE_ASSERT_TRUE(read_file(gui_project.dir / "project.json") != original);

  // Undo/redo cover project data too: undoing everything restores the original canonical project.json.
  const std::string edited = read_file(gui_project.dir / "project.json");
  while (edits.can_undo()) ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(!edits.modified());
  ARCANE_ASSERT_TRUE(ok(gui->save()));
  ARCANE_ASSERT_TRUE(read_file(gui_project.dir / "project.json") == original);
  while (edits.can_redo()) ARCANE_ASSERT_TRUE(ok(edits.redo()));
  ARCANE_ASSERT_TRUE(ok(gui->save()));
  ARCANE_ASSERT_TRUE(read_file(gui_project.dir / "project.json") == edited);
}

void test_hud_drag_coalesces_to_one_undo_step() {
  TempProject project("arcane_edit_session_hud_drag");
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = open(project);
  EditSession edits(*context, registry);
  ARCANE_ASSERT_TRUE(ok(edits.run({"hud.add", {{"type", "bar"}}})));
  edits.end_gesture();

  // One drag of the bar fraction: many frames, one key.
  for (double v : {0.9, 0.7, 0.5, 0.3}) {
    ARCANE_ASSERT_TRUE(ok(edits.run({"hud.set", {{"index", 0}, {"field", "fraction"}, {"value", v}}}, "hud.set:0:fraction")));
  }
  edits.end_gesture();
  // A drag of another field is its own step.
  ARCANE_ASSERT_TRUE(ok(edits.run({"hud.set", {{"index", 0}, {"field", "width"}, {"value", 100.0}}}, "hud.set:0:width")));
  edits.end_gesture();

  const auto& bar = std::get<arcane::HudProgressBar>(context->session().project().hud.elements()[0]);
  ARCANE_ASSERT_TRUE(bar.width == 100.0f && bar.fraction == 0.3f);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));  // width
  ARCANE_ASSERT_TRUE(std::get<arcane::HudProgressBar>(context->session().project().hud.elements()[0]).width == 200.0f);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));  // the whole fraction drag
  ARCANE_ASSERT_TRUE(std::get<arcane::HudProgressBar>(context->session().project().hud.elements()[0]).fraction == 1.0f);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));  // the add
  ARCANE_ASSERT_TRUE(context->session().project().hud.elements().empty());
  ARCANE_ASSERT_TRUE(!edits.can_undo());
}

void test_gui_project_settings_match_cli_model() {
  TempProject gui_project("arcane_edit_session_proj_gui");
  TempProject cli_project("arcane_edit_session_proj_cli");
  CommandRegistry registry;
  register_all_commands(registry);
  auto gui = open(gui_project);
  auto cli = open(cli_project);
  EditSession edits(*gui, registry);
  const auto get = [&](ProjectContext& context) { return registry.execute(context, {"project.get", Json::object()}, {}); };

  auto before = get(*gui);
  ARCANE_ASSERT_TRUE(ok(before));
  const Json original = before->value;
  ARCANE_ASSERT_TRUE(!edits.modified());

  const Invocation rename{"project.set", {{"name", "Renamed"}}};
  const Invocation scene{"project.set", {{"main_scene", "scenes/other.json"}}};
  for (const Invocation& call : {rename, scene}) {
    ARCANE_ASSERT_TRUE(ok(edits.run(call)));
    ARCANE_ASSERT_TRUE(ok(registry.execute(*cli, call, {})));
  }
  auto gui_after = get(*gui);
  auto cli_after = get(*cli);
  ARCANE_ASSERT_TRUE(ok(gui_after) && ok(cli_after));
  ARCANE_ASSERT_TRUE(gui_after->value == cli_after->value);
  ARCANE_ASSERT_TRUE(gui_after->value["name"] == "Renamed" && gui_after->value["main_scene"] == "scenes/other.json");
  ARCANE_ASSERT_TRUE(edits.modified());

  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(get(*gui)->value == original);
  ARCANE_ASSERT_TRUE(!edits.modified());
  ARCANE_ASSERT_TRUE(ok(edits.redo()));
  ARCANE_ASSERT_TRUE(ok(edits.redo()));
  ARCANE_ASSERT_TRUE(get(*gui)->value == cli_after->value);
  ARCANE_ASSERT_TRUE(edits.modified());
}

void test_project_name_typing_coalesces_to_one_undo_step() {
  TempProject project("arcane_edit_session_proj_typing");
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = open(project);
  EditSession edits(*context, registry);
  const std::string original = context->session().project().name;
  for (const char* typed : {"A", "Ab", "Abc"}) {
    ARCANE_ASSERT_TRUE(ok(edits.run({"project.set", {{"name", typed}}}, "project.name")));
  }
  edits.end_gesture();
  ARCANE_ASSERT_TRUE(context->session().project().name == "Abc");
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(context->session().project().name == original);
  ARCANE_ASSERT_TRUE(!edits.can_undo());
}

// Routed editor paths must not write the scene or project data directly any more (AF5B scene, AF6 tiles,
// AF7B HUD, input map and clip Save). What still bypasses commands is listed in docs/editor-command-inventory.md.
void test_audio_physics_particle_props_match_cli() {
  TempProject project("arcane_edit_session_apx");
  CommandRegistry registry;
  register_all_commands(registry);
  auto gui = open(project);
  auto cli = open(project);
  EditSession edits(*gui, registry);
  const std::string original = scene_text(*gui);

  struct Case {
    const char* type;
    const char* name;
    arcane::PropertyValue value;
    Json json;
  };
  const std::vector<Case> cases = {
      {"AudioSource", "volume", 0.25f, 0.25},
      {"AudioSource", "loop", true, true},
      {"AudioSource", "play_on_start", true, true},
      {"RigidBody2D", "gravity_scale", 2.5f, 2.5},
      {"Collider2D", "friction", 0.75f, 0.75},
      {"ParticleEmitter", "rate", 12.5f, 12.5},
  };
  std::vector<std::string> states{original};
  for (const Case& c : cases) {
    ARCANE_ASSERT_TRUE(ok(edits.set_property(1, component_id(*gui, c.type), descriptor(*gui, c.type, c.name), c.value)));
    ARCANE_ASSERT_TRUE(ok(registry.execute(
        *cli, {"prop.set", {{"node", "id:1"}, {"component", c.type}, {"name", c.name}, {"value", c.json}}}, {})));
    ARCANE_ASSERT_TRUE(scene_text(*gui) == scene_text(*cli));
    ARCANE_ASSERT_TRUE(scene_text(*gui) != states.back());
    states.push_back(scene_text(*gui));
  }
  for (std::size_t i = cases.size(); i > 0; --i) {
    ARCANE_ASSERT_TRUE(ok(edits.undo()));
    ARCANE_ASSERT_TRUE(scene_text(*gui) == states[i - 1]);
  }
  ARCANE_ASSERT_TRUE(scene_text(*gui) == original);
  for (std::size_t i = 1; i <= cases.size(); ++i) {
    ARCANE_ASSERT_TRUE(ok(edits.redo()));
    ARCANE_ASSERT_TRUE(scene_text(*gui) == states[i]);
  }
}

void test_module_property_undo_after_reopen() {
  TempProject project("arcane_edit_session_module_reopen");
  CommandRegistry registry;
  register_all_commands(registry);
  std::string saved;
  {
    auto context = open(project);
    EditSession edits(*context, registry);
    ARCANE_ASSERT_TRUE(ok(edits.set_property(1, component_id(*context, "Spinner"),
                                             descriptor(*context, "Spinner", "speed_degrees_per_second"), 123.0f)));
    ARCANE_ASSERT_TRUE(edits.modified());
    ARCANE_ASSERT_TRUE(context->save().has_value());
    edits.mark_saved();
    saved = scene_text(*context);
  }
  auto context = open(project);
  ARCANE_ASSERT_TRUE(scene_text(*context) == saved);
  EditSession edits(*context, registry);
  ARCANE_ASSERT_TRUE(!edits.modified());
  ARCANE_ASSERT_TRUE(ok(edits.set_property(1, component_id(*context, "Spinner"),
                                           descriptor(*context, "Spinner", "speed_degrees_per_second"), 7.0f)));
  ARCANE_ASSERT_TRUE(scene_text(*context) != saved);
  ARCANE_ASSERT_TRUE(ok(edits.undo()));
  ARCANE_ASSERT_TRUE(scene_text(*context) == saved);
  ARCANE_ASSERT_TRUE(!edits.modified());
}

void test_view_state_is_not_authored() {
  TempProject project("arcane_edit_session_view_state");
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = open(project);
  EditSession edits(*context, registry);
  const std::string original = scene_text(*context);

  arcane::editor::Selection selection;
  selection.select(1);
  selection.clear();
  selection.select(1);
  arcane::editor::PlayStopController play;
  ARCANE_ASSERT_TRUE(play.play(context->scene(), context->registry()));
  play.tick(0.016f);
  play.stop();

  ARCANE_ASSERT_TRUE(!edits.modified());
  ARCANE_ASSERT_TRUE(!edits.can_undo());
  ARCANE_ASSERT_TRUE(scene_text(*context) == original);
  ARCANE_ASSERT_TRUE(!edits.undo());
  ARCANE_ASSERT_TRUE(scene_text(*context) == original);
  ARCANE_ASSERT_TRUE(!edits.modified());
}

void test_editor_source_has_no_direct_scene_writes() {
  std::ifstream in(ARCANE_EDITOR_SOURCE_PATH);
  ARCANE_ASSERT_TRUE(in.good());
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string source = ss.str();
  for (const char* forbidden : {"->set_name(", "create_node(", "destroy_node(", "flush_destroyed(", "->reparent(",
                                 "add_component(", "descriptor.set(", "local_transform() =", "auto& transform = ", "sprite->texture",
                                 "->texture =", "->clip =", "->tileset =", "mutable_project()", "hud.add(", ".set_action(",
                                 ".remove_action(", "save_clip_file(", "elements.erase(", ".main_scene =", ".module =", "project().name ="}) {
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
  test_gui_hud_input_matches_cli_model();
  test_hud_drag_coalesces_to_one_undo_step();
  test_gui_project_settings_match_cli_model();
  test_project_name_typing_coalesces_to_one_undo_step();
  test_audio_physics_particle_props_match_cli();
  test_module_property_undo_after_reopen();
  test_view_state_is_not_authored();
  test_editor_source_has_no_direct_scene_writes();
  return 0;
}
