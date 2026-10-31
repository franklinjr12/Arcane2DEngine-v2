// hud.* commands: add/set/remove/move of every element type with exact inverses, validation that changes
// nothing, dry-run and persist of project-level data (project.json written atomically with the scene, CONFLICT
// on an external edit), and undo/redo through EditSession.
#include <filesystem>
#include <fstream>
#include <sstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "arcane_tooling/edit_session.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
using namespace arcane::tooling;
namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct TempProject {
  fs::path dir;
  explicit TempProject(const char* name) : dir(fs::temp_directory_path() / name) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    ComponentRegistry registry;
    register_builtin_components(registry);
    auto session = editor::ProjectSession::create(dir, "Hud", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    ARCANE_ASSERT_TRUE(session->save(registry).has_value());
    std::vector<unsigned char> px(16 * 16 * 4, 255);
    fs::create_directories(dir / "assets");
    ARCANE_ASSERT_TRUE(stbi_write_png((dir / "assets/icon.png").string().c_str(), 16, 16, 4, px.data(), 16 * 4) != 0);
  }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

CommandResult ok(CommandRegistry& r, ProjectContext& c, const char* name, Json args, ExecuteOptions o = {}) {
  auto done = r.execute(c, {name, std::move(args)}, o);
  if (!done) std::fprintf(stderr, "%s: %s (%s)\n", name, done.error().message.c_str(), done.error().code.c_str());
  ARCANE_ASSERT_TRUE(done.has_value());
  return *done;
}

std::string code(CommandRegistry& r, ProjectContext& c, const char* name, Json args, ExecuteOptions o = {}) {
  auto done = r.execute(c, {name, std::move(args)}, o);
  return done ? "" : done.error().code;
}

Json list(CommandRegistry& r, ProjectContext& c) { return ok(r, c, "hud.list", Json::object()).value["elements"]; }

void undo_with(CommandRegistry& r, ProjectContext& c, const CommandResult& result) {
  ARCANE_ASSERT_TRUE(result.inverse.has_value());
  ok(r, c, result.inverse->command.c_str(), result.inverse->args);
}

void test_add_set_remove_move() {
  TempProject project("arcane_hud_cmds");
  CommandRegistry registry;
  register_all_commands(registry);
  auto opened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(opened.has_value());
  ProjectContext& ctx = **opened;
  const AssetId icon = editor::asset_id_for_relative_path("icon.png");

  // Editor '+' defaults.
  ARCANE_ASSERT_TRUE(list(registry, ctx).empty());
  auto button = ok(registry, ctx, "hud.add", {{"type", "button"}});
  ARCANE_ASSERT_TRUE(button.value["index"] == 0 && button.value["id"] == "button");
  ARCANE_ASSERT_TRUE(button.inverse->command == "hud.remove");
  auto text = ok(registry, ctx, "hud.add", {{"type", "text"}});
  ok(registry, ctx, "hud.add", {{"type", "bar"}});
  auto image = ok(registry, ctx, "hud.add", {{"type", "image"}, {"element", {{"id", "icon"}, {"width", 48.0}}}});
  ARCANE_ASSERT_TRUE(image.value["index"] == 3);
  Json elements = list(registry, ctx);
  ARCANE_ASSERT_TRUE(elements.size() == 4);
  ARCANE_ASSERT_TRUE(elements[0]["type"] == "button" && elements[0]["label"] == "Button" && elements[0]["anchor"] == "Center");
  ARCANE_ASSERT_TRUE(elements[1]["type"] == "text" && elements[1]["text"] == "Text" && elements[1]["id"] == "text_1");
  ARCANE_ASSERT_TRUE(elements[1]["color"] == Json::array({1.0, 1.0, 1.0, 1.0}));
  ARCANE_ASSERT_TRUE(elements[2]["type"] == "bar" && elements[2]["id"] == "bar_1" && elements[2]["fraction"] == 1.0);
  ARCANE_ASSERT_TRUE(elements[3]["type"] == "image" && elements[3]["id"] == "icon" && elements[3]["width"] == 48.0);
  ARCANE_ASSERT_TRUE(elements[3]["texture_asset"] == 0);
  ARCANE_ASSERT_TRUE(!elements[3].contains("texture") && !elements[0].contains("state") && !elements[0].contains("focused"));
  ARCANE_ASSERT_TRUE(ok(registry, ctx, "hud.add", {{"type", "button"}}).value["id"] == "button_1");

  // Image fields: texture asset, uv, tint, anchor.
  auto tex = ok(registry, ctx, "hud.set", {{"index", 3}, {"field", "texture_asset"}, {"value", icon}});
  ARCANE_ASSERT_TRUE(tex.value["previous"] == 0 && tex.value["value"] == icon);
  auto uv = ok(registry, ctx, "hud.set", {{"index", 3}, {"field", "uv"}, {"value", Json::array({0.25, 0.0, 0.75, 0.5})}});
  ok(registry, ctx, "hud.set", {{"index", 3}, {"field", "tint"}, {"value", Json::array({1.0, 0.5, 0.25, 0.5})}});
  ok(registry, ctx, "hud.set", {{"index", 3}, {"field", "anchor"}, {"value", "BottomRight"}});
  ok(registry, ctx, "hud.set", {{"index", 3}, {"field", "offset_x"}, {"value", -8.5}});
  elements = list(registry, ctx);
  ARCANE_ASSERT_TRUE(elements[3]["texture_asset"] == icon && elements[3]["uv"] == Json::array({0.25, 0.0, 0.75, 0.5}));
  ARCANE_ASSERT_TRUE(elements[3]["tint"] == Json::array({1.0, 0.5, 0.25, 0.5}) && elements[3]["anchor"] == "BottomRight");
  ARCANE_ASSERT_TRUE(elements[3]["offset_x"] == -8.5);
  undo_with(registry, ctx, uv);
  ARCANE_ASSERT_TRUE(list(registry, ctx)[3]["uv"] == Json::array({0.0, 0.0, 1.0, 1.0}));
  undo_with(registry, ctx, tex);
  ARCANE_ASSERT_TRUE(list(registry, ctx)[3]["texture_asset"] == 0);

  // Other types.
  ok(registry, ctx, "hud.set", {{"index", 1}, {"field", "text"}, {"value", "Score\nHigh"}});
  ok(registry, ctx, "hud.set", {{"index", 1}, {"field", "font_scale"}, {"value", 3.0}});
  ok(registry, ctx, "hud.set", {{"index", 2}, {"field", "fraction"}, {"value", 0.5}});
  ok(registry, ctx, "hud.set", {{"index", 2}, {"field", "fill"}, {"value", Json::array({1.0, 0.0, 0.0, 1.0})}});
  ok(registry, ctx, "hud.set", {{"index", 0}, {"field", "enabled"}, {"value", false}});
  ok(registry, ctx, "hud.set", {{"index", 0}, {"field", "id"}, {"value", "play"}});
  elements = list(registry, ctx);
  ARCANE_ASSERT_TRUE(elements[1]["text"] == "Score\nHigh" && elements[1]["font_scale"] == 3.0);
  ARCANE_ASSERT_TRUE(elements[2]["fraction"] == 0.5 && elements[2]["fill"] == Json::array({1.0, 0.0, 0.0, 1.0}));
  ARCANE_ASSERT_TRUE(elements[0]["enabled"] == false && elements[0]["id"] == "play");

  // No-op set returns no inverse.
  auto same = ok(registry, ctx, "hud.set", {{"index", 2}, {"field", "fraction"}, {"value", 0.5}});
  ARCANE_ASSERT_TRUE(!same.inverse && same.value["changed"] == false);

  // Rejections change nothing.
  const Json before = list(registry, ctx);
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 9}, {"field", "text"}, {"value", "x"}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", -1}, {"field", "text"}, {"value", "x"}}) == "VALUE_INVALID");
  auto unknown = registry.execute(ctx, {"hud.set", {{"index", 2}, {"field", "label"}, {"value", "x"}}});
  ARCANE_ASSERT_TRUE(!unknown && unknown.error().code == "ARGS_INVALID" && unknown.error().hint.find("fraction") != std::string::npos);
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 3}, {"field", "texture_asset"}, {"value", 12345}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 3}, {"field", "anchor"}, {"value", "Middle"}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 3}, {"field", "uv"}, {"value", Json::array({0, 1})}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 2}, {"field", "fraction"}, {"value", 1.5}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 2}, {"field", "width"}, {"value", "wide"}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 2}, {"field", "id"}, {"value", "play"}}) == "VALUE_INVALID");  // duplicate
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.set", {{"index", 2}, {"field", "id"}, {"value", ""}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.add", {{"type", "slider"}}) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.add", {{"type", "text"}, {"index", 9}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.add", {{"type", "text"}, {"element", {{"label", "x"}}}}) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.add", {{"type", "text"}, {"element", {{"id", "play"}}}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.remove", {{"index", 7}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "hud.move", {{"from", 0}, {"to", 5}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(list(registry, ctx) == before);

  // Move, with inverse; moving onto itself is a no-op.
  auto moved = ok(registry, ctx, "hud.move", {{"from", 0}, {"to", 3}});
  elements = list(registry, ctx);
  ARCANE_ASSERT_TRUE(elements[3]["id"] == "play" && elements[0]["type"] == "text");
  undo_with(registry, ctx, moved);
  ARCANE_ASSERT_TRUE(list(registry, ctx) == before);
  ARCANE_ASSERT_TRUE(!ok(registry, ctx, "hud.move", {{"from", 2}, {"to", 2}}).inverse);

  // Remove of each type restores exactly, at the same index.
  for (int index : {3, 2, 1, 0}) {
    auto removed = ok(registry, ctx, "hud.remove", {{"index", index}});
    ARCANE_ASSERT_TRUE(removed.inverse->command == "hud.add");
    ARCANE_ASSERT_TRUE(list(registry, ctx).size() == 4);  // 5 elements before: button_1 shifted
    undo_with(registry, ctx, removed);
    ARCANE_ASSERT_TRUE(list(registry, ctx) == before);
  }
  ARCANE_ASSERT_TRUE(ctx.project_dirty());
}

void test_dry_run_persist_conflict() {
  TempProject project("arcane_hud_persist");
  CommandRegistry registry;
  register_all_commands(registry);
  auto opened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(opened.has_value());
  ProjectContext& ctx = **opened;
  const std::string project_text = slurp(project.dir / "project.json");
  const std::string scene_text = slurp(project.dir / ctx.scene_file());

  // Dry run: result and inverse, memory restored, nothing written, no dirty flags.
  auto dry = ok(registry, ctx, "hud.add", {{"type", "text"}}, {.dry_run = true});
  ARCANE_ASSERT_TRUE(dry.inverse.has_value());
  ARCANE_ASSERT_TRUE(list(registry, ctx).empty() && !ctx.project_dirty());
  ARCANE_ASSERT_TRUE(slurp(project.dir / "project.json") == project_text);

  // Without persist: memory only.
  ok(registry, ctx, "hud.add", {{"type", "text"}});
  ARCANE_ASSERT_TRUE(ctx.project_dirty() && slurp(project.dir / "project.json") == project_text);

  // Persist: project.json canonical and loadable, flags cleared, scene rewritten unchanged.
  ok(registry, ctx, "hud.set", {{"index", 0}, {"field", "text"}, {"value", "Hello"}}, {.persist = true});
  ARCANE_ASSERT_TRUE(!ctx.project_dirty() && !ctx.dirty());
  const std::string saved = slurp(project.dir / "project.json");
  ARCANE_ASSERT_TRUE(saved != project_text && saved.find("Hello") != std::string::npos);
  ARCANE_ASSERT_TRUE(canonicalize_json(saved).value() == saved);
  ARCANE_ASSERT_TRUE(slurp(project.dir / ctx.scene_file()) == scene_text);
  auto reopened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(reopened.has_value());
  ARCANE_ASSERT_TRUE(list(registry, **reopened).size() == 1 && list(registry, **reopened)[0]["text"] == "Hello");

  // External edit of project.json: persist refuses, rolls memory back, writes nothing.
  {
    std::ofstream out(project.dir / "project.json", std::ios::binary | std::ios::app);
    out << " ";
  }
  const std::string edited = slurp(project.dir / "project.json");
  auto conflict = registry.execute(ctx, {"hud.set", {{"index", 0}, {"field", "text"}, {"value", "Bye"}}}, {.persist = true});
  ARCANE_ASSERT_TRUE(!conflict && conflict.error().code == "CONFLICT");
  ARCANE_ASSERT_TRUE(list(registry, ctx)[0]["text"] == "Hello");
  ARCANE_ASSERT_TRUE(slurp(project.dir / "project.json") == edited && slurp(project.dir / ctx.scene_file()) == scene_text);
  // A scene-only command still saves when project data is clean.
  ARCANE_ASSERT_TRUE(ctx.save().has_value());
}

void test_edit_session_undo_redo() {
  TempProject project("arcane_hud_undo");
  CommandRegistry registry;
  register_all_commands(registry);
  auto opened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(opened.has_value());
  ProjectContext& ctx = **opened;
  EditSession session(ctx, registry);
  ARCANE_ASSERT_TRUE(session.run({"hud.add", {{"type", "bar"}}}).has_value());
  ARCANE_ASSERT_TRUE(session.run({"hud.set", {{"index", 0}, {"field", "fraction"}, {"value", 0.25}}}).has_value());
  ARCANE_ASSERT_TRUE(session.undo().has_value() && session.undo().has_value());
  ARCANE_ASSERT_TRUE(list(registry, ctx).empty());
  ARCANE_ASSERT_TRUE(session.redo().has_value() && session.redo().has_value());
  ARCANE_ASSERT_TRUE(list(registry, ctx)[0]["fraction"] == 0.25);
}

Json actions_of(CommandRegistry& r, ProjectContext& c) { return ok(r, c, "input.list", Json::object()).value["actions"]; }

std::string project_text(ProjectContext& c) { return *c.session().project().input_actions.save_canonical_json(); }

void test_input_commands() {
  TempProject project("arcane_input_cmds");
  CommandRegistry registry;
  register_all_commands(registry);
  auto opened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(opened.has_value());
  ProjectContext& ctx = **opened;
  const std::string empty = project_text(ctx);

  auto jump = ok(registry, ctx, "input.add_action", {{"name", "jump"}});
  ARCANE_ASSERT_TRUE(jump.inverse->command == "input.remove_action");
  ok(registry, ctx, "input.add_action", {{"name", "fire"}, {"player", 1}});
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.add_action", {{"name", "jump"}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.add_action", {{"name", ""}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.add_action", {{"name", "x"}, {"player", 4}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(actions_of(registry, ctx).size() == 2);

  auto key = ok(registry, ctx, "input.add_binding", {{"action", "jump"}, {"device", "keyboard"}, {"code", 32}});
  ARCANE_ASSERT_TRUE(key.value["index"] == 0);
  ok(registry, ctx, "input.add_binding",
     {{"action", "jump"}, {"device", "gamepad_axis"}, {"code", 1}, {"axis_sign", -1}, {"deadzone", 0.25}, {"threshold", 0.75}});
  ok(registry, ctx, "input.add_binding", {{"action", "jump"}, {"device", "mouse_button"}, {"code", 0}, {"index", 0}});
  Json list = actions_of(registry, ctx);
  ARCANE_ASSERT_TRUE(list[0]["bindings"].size() == 3 && list[0]["bindings"][0]["device"] == "mouse_button");
  ARCANE_ASSERT_TRUE(list[0]["bindings"][2]["device"] == "gamepad_axis" && list[0]["bindings"][2]["deadzone"] == 0.25);
  ARCANE_ASSERT_TRUE(list[1]["player"] == 1);

  // Invalid arguments change nothing.
  const std::string before = project_text(ctx);
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.add_binding", {{"action", "nope"}, {"device", "keyboard"}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.add_binding", {{"action", "jump"}, {"device", "touch"}}) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.add_binding", {{"action", "jump"}, {"deadzone", 1.0}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.add_binding", {{"action", "jump"}, {"index", 9}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.remove_binding", {{"action", "jump"}, {"index", 3}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.set_binding", {{"action", "jump"}, {"index", 0}, {"field", "pad"}, {"value", 4}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.set_binding", {{"action", "jump"}, {"index", 2}, {"field", "axis_sign"}, {"value", 2}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.set_binding", {{"action", "jump"}, {"index", 2}, {"field", "bogus"}, {"value", 1}}) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.set_player", {{"action", "jump"}, {"player", -2}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(code(registry, ctx, "input.remove_action", {{"action", "nope"}}) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(project_text(ctx) == before);

  // Every inverse restores the canonical text.
  auto set = ok(registry, ctx, "input.set_binding", {{"action", "jump"}, {"index", 2}, {"field", "threshold"}, {"value", 0.5}});
  ARCANE_ASSERT_TRUE(set.value["previous"] == 0.75 && project_text(ctx) != before);
  undo_with(registry, ctx, set);
  ARCANE_ASSERT_TRUE(project_text(ctx) == before);
  auto dev = ok(registry, ctx, "input.set_binding", {{"action", "jump"}, {"index", 1}, {"field", "device"}, {"value", "gamepad_button"}});
  undo_with(registry, ctx, dev);
  ARCANE_ASSERT_TRUE(project_text(ctx) == before);
  auto player = ok(registry, ctx, "input.set_player", {{"action", "jump"}, {"player", 2}});
  ARCANE_ASSERT_TRUE(project_text(ctx) != before);
  undo_with(registry, ctx, player);
  ARCANE_ASSERT_TRUE(project_text(ctx) == before);
  auto rb = ok(registry, ctx, "input.remove_binding", {{"action", "jump"}, {"index", 2}});
  ARCANE_ASSERT_TRUE(rb.inverse->command == "input.add_binding" && rb.inverse->args["index"] == 2);
  undo_with(registry, ctx, rb);
  ARCANE_ASSERT_TRUE(project_text(ctx) == before);
  auto ra = ok(registry, ctx, "input.remove_action", {{"action", "jump"}});
  ARCANE_ASSERT_TRUE(ra.inverse->command == "input.restore_action" && actions_of(registry, ctx).size() == 1);
  undo_with(registry, ctx, ra);
  ARCANE_ASSERT_TRUE(project_text(ctx) == before && actions_of(registry, ctx)[0]["name"] == "jump");

  // dry_run restores; persist + reopen keeps the edits.
  ok(registry, ctx, "input.remove_action", {{"action", "fire"}}, {.dry_run = true});
  ARCANE_ASSERT_TRUE(project_text(ctx) == before);
  ok(registry, ctx, "input.set_player", {{"action", "fire"}, {"player", 3}}, {.persist = true});
  auto reopened = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(reopened.has_value());
  Json again = actions_of(registry, **reopened);
  ARCANE_ASSERT_TRUE(again.size() == 2 && again[1]["player"] == 3 && again[0]["bindings"].size() == 3);
  ARCANE_ASSERT_TRUE(again[0]["bindings"][2]["axis_sign"] == -1 && again[0]["bindings"][2]["threshold"] == 0.75);
  ARCANE_ASSERT_TRUE(!empty.empty());
}

}  // namespace

int main() {
  test_add_set_remove_move();
  test_dry_run_persist_conflict();
  test_edit_session_undo_redo();
  test_input_commands();
  return 0;
}
