// animgraph.* commands: build a graph, reject bad edits without touching the file, and undo through the inverse.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "arcane/assets.hpp"
#include "arcane/builtin_components.hpp"
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

struct Env {
  fs::path dir = fs::temp_directory_path() / "arcane_animgraph_cmds";
  CommandRegistry registry;
  std::unique_ptr<ProjectContext> context;
  Env() {
    std::error_code ec;
    fs::remove_all(dir, ec);
    ComponentRegistry components;
    register_builtin_components(components);
    auto session = editor::ProjectSession::create(dir, "Graphs", components);
    ARCANE_ASSERT_TRUE(session.has_value() && session->save(components).has_value());
    fs::create_directories(dir / "assets");
    std::ofstream(dir / "assets/idle.json") << "{}";
    std::ofstream(dir / "assets/run.json") << "{}";
    register_all_commands(registry);
    auto opened = ProjectContext::open(dir);
    ARCANE_ASSERT_TRUE(opened.has_value());
    context = std::move(*opened);
  }
  ~Env() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  CommandResult run(const char* name, Json args) {
    auto done = registry.execute(*context, {name, std::move(args)});
    if (!done) std::fprintf(stderr, "%s: %s (%s)\n", name, done.error().message.c_str(), done.error().code.c_str());
    ARCANE_ASSERT_TRUE(done.has_value());
    return *done;
  }
  std::string fails(const char* name, Json args) {
    auto done = registry.execute(*context, {name, std::move(args)});
    return done ? "" : done.error().code;
  }
  std::string text() { return slurp(dir / "assets/hero.animgraph.json"); }
};

void test_build_and_undo() {
  Env env;
  const Json g = {{"graph", "assets/hero.animgraph.json"}};
  auto with = [&](Json extra) {
    Json args = g;
    for (auto& [k, v] : extra.items()) args[k] = v;
    return args;
  };
  env.run("animgraph.create", g);
  ARCANE_ASSERT_TRUE(env.fails("animgraph.create", g) == "CONFLICT");
  env.run("animgraph.add_param", with({{"name", "moving"}, {"type", "bool"}}));
  env.run("animgraph.add_state", with({{"name", "idle"}, {"clip", "assets/idle.json"}}));
  const auto run_state = env.run("animgraph.add_state", with({{"name", "run"}, {"clip", "assets/run.json"}, {"speed", 1.5}}));
  ARCANE_ASSERT_TRUE(run_state.value["clip_id"] == asset_id_for_relative_path("run.json"));
  env.run("animgraph.add_event", with({{"state", "run"}, {"frame", 1}, {"name", "step"}}));
  const std::string before_transition = env.text();
  const auto added = env.run("animgraph.add_transition",
                             with({{"from", "idle"}, {"to", "run"}, {"conditions", Json::array({{{"param", "moving"}, {"op", "is_true"}}})}}));
  ARCANE_ASSERT_TRUE(added.value["index"] == 0);

  const Json info = env.run("animgraph.info", g).value;
  ARCANE_ASSERT_TRUE(info["initial"] == "idle" && info["states"].size() == 2 && info["transitions"].size() == 1);
  ARCANE_ASSERT_TRUE(info["states"][1]["events"][0]["name"] == "step");

  // Bad edits fail and leave the file as it was.
  const std::string before = env.text();
  ARCANE_ASSERT_TRUE(env.fails("animgraph.add_transition", with({{"from", "idle"}, {"to", "gone"}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("animgraph.add_transition", with({{"from", "idle"}, {"to", "run"},
      {"conditions", Json::array({{{"param", "moving"}, {"op", "gt"}, {"value", 1}}})}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.fails("animgraph.add_state", with({{"name", "x"}, {"clip", "assets/missing.json"}})) == "IO_ERROR");
  ARCANE_ASSERT_TRUE(env.fails("animgraph.remove_state", with({{"name", "run"}})) == "CONFLICT");
  ARCANE_ASSERT_TRUE(env.fails("animgraph.remove_param", with({{"name", "moving"}})) == "CONFLICT");
  ARCANE_ASSERT_TRUE(env.fails("animgraph.add_event", with({{"state", "nope"}, {"frame", 0}, {"name", "e"}})) == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(env.text() == before);

  // The inverse restores the exact bytes.
  const auto removed = env.run("animgraph.remove_transition", with({{"index", 0}}));
  ARCANE_ASSERT_TRUE(env.text() == before_transition);
  ARCANE_ASSERT_TRUE(removed.inverse.has_value());
  env.run(removed.inverse->command.c_str(), removed.inverse->args);
  ARCANE_ASSERT_TRUE(env.text() == before);
  const auto no_op = env.run("animgraph.set_state", with({{"name", "run"}, {"speed", 1.5}}));
  ARCANE_ASSERT_TRUE(no_op.value["changed"] == false && !no_op.inverse.has_value());
}

}  // namespace

int main() {
  test_build_and_undo();
  std::puts("test_animgraph_commands OK");
  return 0;
}
