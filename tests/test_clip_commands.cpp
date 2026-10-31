// clip.* commands: slice UVs against grid_cell_uv, create vs build_clip, frame edits, overwrite refusal, off-grid
// reopen errors that leave the file untouched, and inverses that restore the exact bytes.
#include <filesystem>
#include <fstream>
#include <sstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "arcane/builtin_components.hpp"
#include "arcane_editor/clip_authoring.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
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

// 64x32 sheet: a 4x2 grid of 16x16 cells.
struct TempProject {
  fs::path dir;
  explicit TempProject(const char* name) : dir(fs::temp_directory_path() / name) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    ComponentRegistry registry;
    register_builtin_components(registry);
    auto session = editor::ProjectSession::create(dir, "Clips", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    ARCANE_ASSERT_TRUE(session->save(registry).has_value());
    std::vector<unsigned char> px(64 * 32 * 4, 255);
    fs::create_directories(dir / "assets");
    ARCANE_ASSERT_TRUE(stbi_write_png((dir / "assets/sheet.png").string().c_str(), 64, 32, 4, px.data(), 64 * 4) != 0);
    std::ofstream(dir / "assets/notes.txt") << "not an image";
  }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

CommandResult ok(CommandRegistry& r, ProjectContext& c, const char* name, Json args) {
  auto done = r.execute(c, {name, std::move(args)});
  if (!done) std::fprintf(stderr, "%s: %s (%s)\n", name, done.error().message.c_str(), done.error().code.c_str());
  ARCANE_ASSERT_TRUE(done.has_value());
  return *done;
}

std::string code(CommandRegistry& r, ProjectContext& c, const char* name, Json args) {
  auto done = r.execute(c, {name, std::move(args)});
  return done ? "" : done.error().code;
}

Json grid(Json extra = Json::object()) {
  Json g = {{"sheet", "assets/sheet.png"}, {"columns", 4}, {"rows", 2}};
  for (auto& [k, v] : extra.items()) g[k] = v;
  return g;
}

struct Env {
  TempProject project;
  CommandRegistry registry;
  std::unique_ptr<ProjectContext> context;
  explicit Env(const char* name) : project(name) {
    register_all_commands(registry);
    auto opened = ProjectContext::open(project.dir);
    ARCANE_ASSERT_TRUE(opened.has_value());
    context = std::move(*opened);
  }
  CommandResult run(const char* name, Json args) { return ok(registry, *context, name, std::move(args)); }
  std::string fails(const char* name, Json args) { return code(registry, *context, name, std::move(args)); }
  std::string text(const char* file) { return slurp(project.dir / file); }
  Invocation inverse_of(const CommandResult& r) {
    ARCANE_ASSERT_TRUE(r.inverse.has_value());
    return *r.inverse;
  }
  void undo(const CommandResult& r) {
    const Invocation inv = inverse_of(r);
    run(inv.command.c_str(), inv.args);
  }
};

Json with(Json base, Json extra) {
  for (auto& [k, v] : extra.items()) base[k] = v;
  return base;
}

void test_slice() {
  Env env("arcane_clip_slice");
  const auto result = env.run("clip.slice", grid());
  ARCANE_ASSERT_TRUE(result.value["sheet_width"] == 64 && result.value["sheet_height"] == 32);
  ARCANE_ASSERT_TRUE(result.value["cells"].size() == 8);
  const editor::SheetGrid g{4, 2, 0, 0, 0, 0};
  const editor::SheetGrid padded{3, 1, 10, 12, 2, 3};
  for (int i = 0; i < 8; ++i) {
    const UvRect uv = editor::grid_cell_uv(g, 64, 32, i);
    ARCANE_ASSERT_TRUE(result.value["cells"][static_cast<std::size_t>(i)]["index"] == i);
    ARCANE_ASSERT_TRUE(result.value["cells"][static_cast<std::size_t>(i)]["uv"] == Json::array({uv.u0, uv.v0, uv.u1, uv.v1}));
  }
  const auto custom = env.run("clip.slice", {{"sheet", "assets/sheet.png"}, {"columns", 3}, {"rows", 1}, {"frame_width", 10},
                                             {"frame_height", 12}, {"margin", 2}, {"spacing", 3}});
  const UvRect last = editor::grid_cell_uv(padded, 64, 32, 2);
  ARCANE_ASSERT_TRUE(custom.value["cells"][2]["uv"] == Json::array({last.u0, last.v0, last.u1, last.v1}));
  // Rejections: bad grid values, a grid with empty cells, missing or non-PNG sheets, bad paths.
  ARCANE_ASSERT_TRUE(env.fails("clip.slice", with(grid(), {{"columns", 0}})) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.slice", with(grid(), {{"columns", 100}})) == "VALUE_INVALID");  // 64/100 = 0 px cells
  ARCANE_ASSERT_TRUE(env.fails("clip.slice", with(grid(), {{"sheet", "assets/missing.png"}})) == "IO_ERROR");
  ARCANE_ASSERT_TRUE(env.fails("clip.slice", with(grid(), {{"sheet", "assets/notes.txt"}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.slice", with(grid(), {{"sheet", "../sheet.png"}})) == "ARGS_INVALID");
}

void test_create_and_info() {
  Env env("arcane_clip_create");
  const auto created = env.run("clip.create", with(grid(), {{"clip", "assets/anim/run.json"}, {"from", 1}, {"to", 3}, {"fps", 8}, {"name", "run"}}));
  ARCANE_ASSERT_TRUE(created.value["frames"] == 3);
  editor::ClipDraft draft;
  draft.name = "run";
  draft.grid = {4, 2, 0, 0, 0, 0};
  draft.fps = 8.0f;
  draft.add_range(1, 3);
  const AnimationClip expected = editor::build_clip(draft, 64, 32);
  auto loaded = editor::load_clip_file(env.project.dir / "assets/anim/run.json");
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->name == "run" && loaded->loop && loaded->frames.size() == 3);
  for (std::size_t i = 0; i < 3; ++i) {
    ARCANE_ASSERT_TRUE(loaded->frames[i].uv.u0 == expected.frames[i].uv.u0 && loaded->frames[i].uv.v1 == expected.frames[i].uv.v1);
    ARCANE_ASSERT_TRUE(loaded->frames[i].duration_seconds == expected.frames[i].duration_seconds);
  }
  // Explicit cells, repeated cells and per-frame durations; loop off.
  env.run("clip.create", with(grid(), {{"clip", "assets/anim/blink.json"}, {"cells", Json::array({0, 5, 0})},
                                       {"durations", Json::array({0.5, 0, 0.25})}, {"loop", false}}));
  const auto info = env.run("clip.info", with(grid(), {{"clip", "assets/anim/blink.json"}}));
  ARCANE_ASSERT_TRUE(info.value["cells"] == Json::array({0, 5, 0}));
  ARCANE_ASSERT_TRUE(info.value["loop"] == false && info.value["frames"].size() == 3);
  ARCANE_ASSERT_TRUE(info.value["durations"] == Json::array({0.5f, 1.0f / 10.0f, 0.25f}));
  ARCANE_ASSERT_TRUE(env.run("clip.info", {{"clip", "assets/anim/blink.json"}}).value["frames"][1]["seconds"] == 0.1f);

  // Overwrite refusal and argument checks leave files alone.
  const std::string before = env.text("assets/anim/run.json");
  ARCANE_ASSERT_TRUE(env.fails("clip.create", with(grid(), {{"clip", "assets/anim/run.json"}, {"cells", Json::array({0})}})) == "CONFLICT");
  ARCANE_ASSERT_TRUE(env.text("assets/anim/run.json") == before);
  const Json base = with(grid(), {{"clip", "assets/anim/bad.json"}});
  ARCANE_ASSERT_TRUE(env.fails("clip.create", base) == "ARGS_INVALID");                                          // no cells
  ARCANE_ASSERT_TRUE(env.fails("clip.create", with(base, {{"cells", Json::array({0})}, {"from", 0}, {"to", 1}})) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.create", with(base, {{"cells", Json::array({8})}})) == "VALUE_INVALID");    // off the grid
  ARCANE_ASSERT_TRUE(env.fails("clip.create", with(base, {{"cells", Json::array({0, 1})}, {"durations", Json::array({1})}})) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.create", with(base, {{"cells", Json::array({0})}, {"fps", 0}})) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.create", with(base, {{"from", 0}})) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(!fs::exists(env.project.dir / "assets/anim/bad.json"));

  // Overwrite replaces the file, and its inverse brings the old bytes back.
  const auto replaced = env.run("clip.create", with(grid(), {{"clip", "assets/anim/run.json"}, {"cells", Json::array({7})}, {"overwrite", true}}));
  ARCANE_ASSERT_TRUE(env.text("assets/anim/run.json") != before);
  env.undo(replaced);
  ARCANE_ASSERT_TRUE(env.text("assets/anim/run.json") == before);
  // The inverse of a fresh create deletes the file; undoing that restores it.
  const auto fresh = env.run("clip.create", with(grid(), {{"clip", "assets/anim/new.json"}, {"cells", Json::array({2})}}));
  const std::string created_text = env.text("assets/anim/new.json");
  ARCANE_ASSERT_TRUE(fresh.inverse && fresh.inverse->args["absent"] == true);
  const auto removed = env.run(fresh.inverse->command.c_str(), fresh.inverse->args);
  ARCANE_ASSERT_TRUE(!fs::exists(env.project.dir / "assets/anim/new.json"));
  env.undo(removed);
  ARCANE_ASSERT_TRUE(env.text("assets/anim/new.json") == created_text);
  ARCANE_ASSERT_TRUE(env.fails("clip.restore", {{"clip", "assets/anim/new.json"}}) == "ARGS_INVALID");
}

void test_frame_edits() {
  Env env("arcane_clip_edits");
  const Json clip = {{"clip", "assets/run.json"}};
  env.run("clip.create", with(grid(), {{"clip", "assets/run.json"}, {"cells", Json::array({0, 1, 2})}, {"fps", 10}, {"name", "run"}}));
  auto cells = [&] { return env.run("clip.info", with(grid(), clip)).value["cells"]; };
  auto durations = [&] { return env.run("clip.info", with(grid(), clip)).value["durations"]; };
  std::vector<std::string> history{env.text("assets/run.json")};

  auto edit = [&](const char* name, Json extra) {
    const Json base = std::string(name) == "clip.set" ? clip : with(grid(), clip);  // clip.set takes no grid
    const auto result = env.run(name, with(base, std::move(extra)));
    ARCANE_ASSERT_TRUE(result.value["changed"] == true);
    ARCANE_ASSERT_TRUE(env.text("assets/run.json") != history.back());
    history.push_back(env.text("assets/run.json"));
    return result;
  };

  const auto added = edit("clip.add_frames", {{"from", 6}, {"to", 4}, {"index", 1}});
  ARCANE_ASSERT_TRUE(added.value["added"] == 3);
  ARCANE_ASSERT_TRUE(cells() == Json::array({0, 6, 5, 4, 1, 2}));
  const auto set_cell = edit("clip.set_frame", {{"index", 0}, {"cell", 7}});
  const auto set_time = edit("clip.set_frame", {{"index", 2}, {"seconds", 0.5}});
  ARCANE_ASSERT_TRUE(cells() == Json::array({7, 6, 5, 4, 1, 2}));
  ARCANE_ASSERT_TRUE(durations()[2] == 0.5);
  const auto moved = edit("clip.move_frame", {{"from", 2}, {"to", 0}});  // the 0.5s frame travels with its cell
  ARCANE_ASSERT_TRUE(cells() == Json::array({5, 7, 6, 4, 1, 2}));
  ARCANE_ASSERT_TRUE(durations()[0] == 0.5);
  const auto removed = edit("clip.remove_frame", {{"index", 5}});
  ARCANE_ASSERT_TRUE(cells() == Json::array({5, 7, 6, 4, 1}));
  const auto renamed = edit("clip.set", {{"name", "dash"}, {"loop", false}});
  const auto info = env.run("clip.info", clip);
  ARCANE_ASSERT_TRUE(info.value["name"] == "dash" && info.value["loop"] == false && info.value["frames"].size() == 5);

  // Inverses, newest first, walk back through every earlier byte state.
  const CommandResult* order[] = {&renamed, &removed, &moved, &set_time, &set_cell, &added};
  for (const CommandResult* step : order) {
    env.undo(*step);
    history.pop_back();
    ARCANE_ASSERT_TRUE(env.text("assets/run.json") == history.back());
  }

  // No-ops write nothing and have no inverse.
  const std::string stable = env.text("assets/run.json");
  ARCANE_ASSERT_TRUE(!env.run("clip.set_frame", with(with(grid(), clip), {{"index", 0}, {"cell", 0}})).inverse);
  ARCANE_ASSERT_TRUE(!env.run("clip.move_frame", with(with(grid(), clip), {{"from", 1}, {"to", 1}})).inverse);
  ARCANE_ASSERT_TRUE(!env.run("clip.set", with(clip, {{"name", "run"}})).inverse);
  ARCANE_ASSERT_TRUE(env.text("assets/run.json") == stable);

  // Rejected edits change nothing.
  const Json g = with(grid(), clip);
  ARCANE_ASSERT_TRUE(env.fails("clip.add_frames", with(g, {{"from", 0}, {"to", 8}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.add_frames", with(g, {{"from", 0}, {"to", 1}, {"index", 4}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.remove_frame", with(g, {{"index", 3}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.move_frame", with(g, {{"from", 0}, {"to", 3}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.set_frame", with(g, {{"index", 0}})) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.set_frame", with(g, {{"index", 0}, {"cell", 8}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.set_frame", with(g, {{"index", 0}, {"seconds", 0}})) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.set", clip) == "ARGS_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.remove_frame", with(g, {{"clip", "assets/none.json"}, {"index", 0}})) == "IO_ERROR");
  ARCANE_ASSERT_TRUE(env.text("assets/run.json") == stable);
}

void test_off_grid_reopen() {
  Env env("arcane_clip_offgrid");
  env.run("clip.create", with(grid(), {{"clip", "assets/run.json"}, {"cells", Json::array({0, 1, 2})}}));
  const std::string before = env.text("assets/run.json");
  // The clip was cut on 4x2 cells of 16x16; a 3x1 grid does not contain those frames.
  const Json other = {{"clip", "assets/run.json"}, {"sheet", "assets/sheet.png"}, {"columns", 3}, {"rows", 1}};
  for (const char* name : {"clip.add_frames", "clip.remove_frame", "clip.move_frame", "clip.set_frame"}) {
    Json args = other;
    if (std::string(name) == "clip.add_frames") args.update(Json{{"from", 0}, {"to", 1}});
    if (std::string(name) == "clip.remove_frame") args["index"] = 0;
    if (std::string(name) == "clip.move_frame") args.update(Json{{"from", 0}, {"to", 1}});
    if (std::string(name) == "clip.set_frame") args.update(Json{{"index", 0}, {"cell", 1}});
    auto done = env.registry.execute(*env.context, {name, args});
    ARCANE_ASSERT_TRUE(!done.has_value());
    ARCANE_ASSERT_TRUE(done.error().code == "VALUE_INVALID");
    ARCANE_ASSERT_TRUE(done.error().message.find("frame ") != std::string::npos);
    ARCANE_ASSERT_TRUE(env.text("assets/run.json") == before);
  }
  auto info = env.registry.execute(*env.context, {"clip.info", other});
  ARCANE_ASSERT_TRUE(!info.has_value() && info.error().code == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("clip.info", {{"clip", "assets/run.json"}, {"sheet", "assets/sheet.png"}}) == "ARGS_INVALID");
  // clip.set needs no grid and still works on such a clip.
  env.run("clip.set", {{"clip", "assets/run.json"}, {"loop", false}});
}

}  // namespace

int main() {
  test_slice();
  test_create_and_info();
  test_frame_edits();
  test_off_grid_reopen();
  std::puts("test_clip_commands OK");
  return 0;
}
