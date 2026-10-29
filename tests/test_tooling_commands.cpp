// Headless tests for arcane_tooling: the command registry, typed property
// commands against a builtin type (Camera2D, Sprite) and a native module
// type (sample_game's Spinner), undo/redo, validation failures that must
// not write anything, module-load failure, conflict detection and context
// lifetime (the module stays loaded until the context is destroyed).
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/module.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane::tooling;
namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void spit(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

struct TempProject {
  fs::path dir = fs::temp_directory_path() / "arcane_tooling_test";
  fs::path scene() const { return dir / "scenes" / "main.scene.json"; }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  // One node (id 1) holding Spinner (module), Camera2D and Sprite (builtin).
  TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
    ARCANE_ASSERT_TRUE(module.has_value());
    {
      arcane::ComponentRegistry registry;
      arcane::register_builtin_components(registry);
      module->api()->register_types(registry);
      auto session = arcane::editor::ProjectSession::create(dir, "Tooling", registry);
      ARCANE_ASSERT_TRUE(session.has_value());
      arcane::Node* node = session->scene().create_node("Player");
      session->scene().add_component(node, "Spinner", registry);
      session->scene().add_component(node, "Camera2D", registry);
      session->scene().add_component(node, "Sprite", registry);
      ARCANE_ASSERT_TRUE(session->save(registry).has_value());
    }  // session and registry die before the module
  }
};

std::unique_ptr<ProjectContext> open(const TempProject& project) {
  auto context = ProjectContext::open(project.dir, ARCANE_SAMPLE_GAME_PATH);
  if (!context) std::fprintf(stderr, "open failed: %s\n", context.error().message.c_str());
  ARCANE_ASSERT_TRUE(context.has_value());
  return std::move(*context);
}

Invocation set(const char* component, const char* name, Json value, const char* node = "id:1") {
  return {"prop.set", {{"node", node}, {"component", component}, {"name", name}, {"value", std::move(value)}}};
}

Json get(CommandRegistry& registry, ProjectContext& context, const char* component, const char* name) {
  auto result = registry.execute(context, {"prop.get", {{"node", "id:1"}, {"component", component}, {"name", name}}});
  ARCANE_ASSERT_TRUE(result.has_value());
  return result->value["value"];
}

CommandRegistry make_registry() {
  CommandRegistry registry;
  register_property_commands(registry);
  return registry;
}

void test_module_property_set_persist_undo_redo() {
  TempProject project;
  CommandRegistry registry = make_registry();
  CommandHistory history;
  const std::string original = slurp(project.scene());
  {
    auto context = open(project);
    ExecuteOptions persist{.persist = true};

    ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "label") == "spinner");
    Invocation call = set("Spinner", "speed_degrees_per_second", 90.5);
    auto result = registry.execute(*context, call, persist);
    ARCANE_ASSERT_TRUE(result.has_value());
    ARCANE_ASSERT_TRUE(result->value["changed"] == true);
    ARCANE_ASSERT_TRUE(result->inverse.has_value());
    history.record(call, *result);
    Invocation label = set("Spinner", "label", "hello");
    auto labeled = registry.execute(*context, label, persist);
    ARCANE_ASSERT_TRUE(labeled.has_value());
    history.record(label, *labeled);
    ARCANE_ASSERT_TRUE(!context->dirty());
  }
  {
    // Reopened from disk: both edits persisted.
    auto context = open(project);
    ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "speed_degrees_per_second") == 90.5);
    ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "label") == "hello");
    ARCANE_ASSERT_TRUE(history.undo_depth() == 2);
    ARCANE_ASSERT_TRUE(history.undo(registry, *context).has_value());  // memory only
    ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "label") == "spinner");
    ARCANE_ASSERT_TRUE(context->dirty());
    ARCANE_ASSERT_TRUE(history.redo(registry, *context).has_value());
    ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "label") == "hello");
    ARCANE_ASSERT_TRUE(history.undo(registry, *context).has_value());
    ARCANE_ASSERT_TRUE(history.undo(registry, *context).has_value());
    ARCANE_ASSERT_TRUE(!history.can_undo() && history.can_redo());
    ARCANE_ASSERT_TRUE(context->save().has_value());
  }
  ARCANE_ASSERT_TRUE(slurp(project.scene()) == original);  // undo restored the exact bytes
}

void test_builtin_types_and_value_kinds() {
  TempProject project;
  CommandRegistry registry = make_registry();
  auto context = open(project);
  const ExecuteOptions persist{.persist = true};
  ARCANE_ASSERT_TRUE(registry.execute(*context, set("Camera2D", "zoom", 2), persist).has_value());
  ARCANE_ASSERT_TRUE(get(registry, *context, "Camera2D", "zoom") == 2.0);
  ARCANE_ASSERT_TRUE(registry.execute(*context, set("Camera2D", "deadzone", Json::array({1.5, 2})), persist).has_value());
  ARCANE_ASSERT_TRUE(get(registry, *context, "Camera2D", "deadzone") == Json::array({1.5, 2.0}));
  ARCANE_ASSERT_TRUE(registry.execute(*context, set("Camera2D", "primary", true), persist).has_value());
  ARCANE_ASSERT_TRUE(registry.execute(*context, set("Camera2D", "target", 1), persist).has_value());  // existing node
  ARCANE_ASSERT_TRUE(registry.execute(*context, set("Sprite", "tint", Json::array({0.1, 0.2, 0.3, 1})), persist).has_value());
  ARCANE_ASSERT_TRUE(get(registry, *context, "Sprite", "tint") == Json::array({0.1, 0.2, 0.3, 1.0}));
  ARCANE_ASSERT_TRUE(registry.execute(*context, set("Sprite", "texture", 77), persist).has_value());

  // Selectors: by component id and by node path.
  auto listed = registry.execute(*context, {"prop.list", {{"node", "path:/Player"}, {"component", "Sprite"}}});
  ARCANE_ASSERT_TRUE(listed.has_value());
  ARCANE_ASSERT_TRUE(listed->value["properties"].size() == 4);
  const unsigned component_id = listed->value["component_id"];
  auto by_id = registry.execute(*context, {"prop.get", {{"node", "id:1"}, {"component", component_id}, {"name", "texture"}}});
  ARCANE_ASSERT_TRUE(by_id.has_value() && by_id->value["value"] == 77);

  // Same value again: no change, no inverse, nothing written.
  auto again = registry.execute(*context, set("Camera2D", "zoom", 2.0), persist);
  ARCANE_ASSERT_TRUE(again.has_value() && again->value["changed"] == false && !again->inverse);
}

void test_failures_change_nothing() {
  TempProject project;
  CommandRegistry registry = make_registry();
  const std::string original = slurp(project.scene());
  auto context = open(project);
  const ExecuteOptions persist{.persist = true};

  struct Case {
    Invocation call;
    const char* code;
  };
  const Case cases[] = {
      {set("Spinner", "speed_degrees_per_second", "fast"), "VALUE_INVALID"},      // wrong type
      {set("Spinner", "label", 5), "VALUE_INVALID"},
      {set("Camera2D", "deadzone", Json::array({1})), "VALUE_INVALID"},          // wrong arity
      {set("Camera2D", "zoom", 1e300), "VALUE_INVALID"},                          // not a binary32
      {set("Camera2D", "primary", 1), "VALUE_INVALID"},
      {set("Camera2D", "target", 999), "NODE_NOT_FOUND"},                         // dangling reference
      {set("Camera2D", "target", -1), "VALUE_INVALID"},
      {set("Spinner", "nope", 1.0), "PROPERTY_UNKNOWN"},
      {set("Nothing", "x", 1.0), "COMPONENT_NOT_FOUND"},
      {set("Spinner", "label", "x", "id:42"), "NODE_NOT_FOUND"},
      {set("Spinner", "label", "x", "path:/Nobody"), "NODE_NOT_FOUND"},
      {set("Spinner", "label", "x", "42"), "ARGS_INVALID"},
      {{"prop.set", {{"node", "id:1"}, {"component", "Spinner"}, {"name", "label"}}}, "ARGS_INVALID"},   // missing value
      {{"prop.set", {{"node", "id:1"}, {"component", "Spinner"}, {"name", "label"}, {"value", "x"}, {"extra", 1}}}, "ARGS_INVALID"},
      {{"no.such.command", Json::object()}, "UNKNOWN_COMMAND"},
  };
  for (const Case& c : cases) {
    auto result = registry.execute(*context, c.call, persist);
    if (result || result.error().code != c.code) {
      std::fprintf(stderr, "case %s expected %s\n", c.call.command.c_str(), c.code);
    }
    ARCANE_ASSERT_TRUE(!result.has_value() && result.error().code == c.code);
    ARCANE_ASSERT_TRUE(exit_code(result.error()) != 0);
    ARCANE_ASSERT_TRUE(error_json(result.error())["error"]["code"] == c.code);
  }
  ARCANE_ASSERT_TRUE(slurp(project.scene()) == original);
  ARCANE_ASSERT_TRUE(!context->dirty());
  ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "label") == "spinner");
}

void test_dry_run_and_conflict_roll_back() {
  TempProject project;
  CommandRegistry registry = make_registry();
  const std::string original = slurp(project.scene());
  auto context = open(project);

  auto dry = registry.execute(*context, set("Spinner", "label", "dry"), ExecuteOptions{.persist = true, .dry_run = true});
  ARCANE_ASSERT_TRUE(dry.has_value() && dry->value["changed"] == true);
  ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "label") == "spinner");
  ARCANE_ASSERT_TRUE(!context->dirty() && slurp(project.scene()) == original);

  // Somebody else edits the scene: the save is refused, memory goes back, their file survives.
  const std::string external = original + " ";
  spit(project.scene(), external);
  auto conflict = registry.execute(*context, set("Spinner", "label", "mine"), ExecuteOptions{.persist = true});
  ARCANE_ASSERT_TRUE(!conflict.has_value() && conflict.error().code == "CONFLICT");
  ARCANE_ASSERT_TRUE(exit_code(conflict.error()) == 5);
  ARCANE_ASSERT_TRUE(slurp(project.scene()) == external);
  ARCANE_ASSERT_TRUE(get(registry, *context, "Spinner", "label") == "spinner");
}

void test_open_failures_write_nothing() {
  TempProject project;
  const std::string original = slurp(project.scene());

  auto missing_module = ProjectContext::open(project.dir, (project.dir / "no_such_module.so").string());
  ARCANE_ASSERT_TRUE(!missing_module.has_value() && missing_module.error().code == "MODULE_LOAD_FAILED");
  ARCANE_ASSERT_TRUE(exit_code(missing_module.error()) == 6);

  // Without the module the scene's Spinner type is unknown: refuse, never drop it.
  auto no_module = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(!no_module.has_value() && no_module.error().code == "SCHEMA_INVALID");

  auto no_project = ProjectContext::open(project.dir / "elsewhere", ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(!no_project.has_value() && no_project.error().code == "PROJECT_NOT_FOUND");
  ARCANE_ASSERT_TRUE(exit_code(no_project.error()) == 3);

  // A scene carrying a property the module does not declare is refused, not loaded leniently.
  std::string unknown = original;
  const std::size_t at = unknown.find("\"label\"");
  ARCANE_ASSERT_TRUE(at != std::string::npos);
  unknown.insert(at, "\"bogus\": 1,\n          ");
  spit(project.scene(), unknown);
  auto refused = ProjectContext::open(project.dir, ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(!refused.has_value() && refused.error().code == "SCHEMA_INVALID");
  ARCANE_ASSERT_TRUE(slurp(project.scene()) == unknown);
}

void test_context_lifetime_and_registry_listing() {
  TempProject project;
  CommandRegistry registry = make_registry();
  for (int i = 0; i < 3; ++i) {  // module is loaded and unloaded each time, after scene and registry
    auto context = open(project);
    ARCANE_ASSERT_TRUE(registry.execute(*context, set("Spinner", "label", "x" + std::to_string(i))).has_value());
  }
  const auto specs = registry.list();
  ARCANE_ASSERT_TRUE(specs.size() == 3 && specs[0]->name == "prop.get" && specs[2]->name == "prop.set");
  ARCANE_ASSERT_TRUE(specs[2]->mutating && !specs[0]->mutating);
}

// Every authored Camera2D setting goes through prop.set, persists, and a reopened project replays the same
// deterministic shake as the in-memory camera.
void test_camera2d_settings_round_trip() {
  TempProject project;
  CommandRegistry registry = make_registry();
  ExecuteOptions persist{.persist = true};
  {
    auto context = open(project);
    const std::pair<const char*, Json> settings[] = {
        {"zoom", 2}, {"smoothing", 4}, {"deadzone", Json::array({3, 4})}, {"bounds_enabled", true},
        {"bounds_min", Json::array({-500, -400})}, {"bounds_max", Json::array({500, 400})}, {"pixel_snap", true},
        {"shake_amplitude", 8}, {"shake_duration", 1}, {"shake_seed", 42}};
    for (const auto& [name, value] : settings) {
      ARCANE_ASSERT_TRUE(registry.execute(*context, set("Camera2D", name, value), persist).has_value());
    }
  }
  auto replay = [&](ProjectContext& context) {
    auto* camera = static_cast<arcane::Camera2D*>(context.scene().find_node(1)->find_component("Camera2D"));
    context.scene().update(0.25f);  // starts components (and the shake), then advances one tick
    std::vector<arcane::Vec2> centers;
    for (int i = 0; i < 4; ++i) {
      context.scene().update(0.25f);
      centers.push_back(camera->view_center(100, 100));
    }
    return centers;
  };
  auto first_context = open(project);
  auto second_context = open(project);
  const auto first = replay(*first_context);
  const auto second = replay(*second_context);
  ARCANE_ASSERT_TRUE(first.size() == second.size());
  for (std::size_t i = 0; i < first.size(); ++i)
    ARCANE_ASSERT_TRUE(first[i].x == second[i].x && first[i].y == second[i].y);
  ARCANE_ASSERT_TRUE(first[0].x != first[1].x || first[0].y != first[1].y);  // actually shaking
  ARCANE_ASSERT_TRUE(get(registry, *first_context, "Camera2D", "shake_seed") == 42);
  // Out-of-range values fail validation and change nothing.
  auto bad = registry.execute(*first_context, set("Camera2D", "shake_seed", 1.5), persist);
  ARCANE_ASSERT_TRUE(!bad.has_value());
}

}  // namespace

int main() {
  test_module_property_set_persist_undo_redo();
  test_builtin_types_and_value_kinds();
  test_failures_change_nothing();
  test_dry_run_and_conflict_roll_back();
  test_open_failures_write_nothing();
  test_context_lifetime_and_registry_listing();
  test_camera2d_settings_round_trip();
  return 0;
}
