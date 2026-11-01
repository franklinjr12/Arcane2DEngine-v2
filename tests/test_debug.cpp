// FE23: debug overlay data (frame timer, text, config in project.json, debug.* commands) and the headless
// benchmark (run_benchmark, bench.run). No window, GL or audio.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/debug.hpp"
#include "arcane/project.hpp"
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

void test_overlay_data() {
  FrameTimer timer;
  for (int i = 0; i < 31; ++i) timer.tick(1.0 / 60.0);  // the 0.5 s window closes on the 31st tick
  ARCANE_ASSERT_TRUE(timer.fps() > 59.9 && timer.fps() < 60.1);
  ARCANE_ASSERT_TRUE(timer.frame_ms() > 16.6 && timer.frame_ms() < 16.7);

  FrameStats stats{60.0, 16.67, 3, 120, 5, 40};
  ARCANE_ASSERT_TRUE(format_debug_overlay(stats) ==
                     "FPS 60.0\nFrame 16.67 ms\nDraw calls 3\nSprites 120\nBodies 5\nParticles 40");
  Hud hud;
  fill_debug_overlay_hud(hud, stats, DebugConfig{});
  fill_debug_overlay_hud(hud, stats, DebugConfig{true, 2.0f});  // replaces, not appends
  ARCANE_ASSERT_TRUE(hud.elements().size() == 1);
  ARCANE_ASSERT_TRUE(std::get<HudText>(hud.elements()[0]).font_scale == 2.0f);
}

void test_benchmark() {
  BenchConfig config{.sprites = 50, .emitters = 3, .particles_per_emitter = 20, .bodies = 10, .frames = 5};
  auto result = run_benchmark(config);
  ARCANE_ASSERT_TRUE(result.has_value());
  ARCANE_ASSERT_TRUE(result->live_particles == 60);
  ARCANE_ASSERT_TRUE(result->physics_bodies == 11);  // bodies + the static floor
  ARCANE_ASSERT_TRUE(result->total_ms > 0.0 && result->max_frame_ms >= result->avg_frame_ms);

  config.frames = 0;
  ARCANE_ASSERT_TRUE(!run_benchmark(config).has_value());
  config.frames = 1;
  config.particles_per_emitter = 257;
  ARCANE_ASSERT_TRUE(!run_benchmark(config).has_value());
}

void test_bench_json() {
  auto doc = run_bench_json({{"sprites", 10}, {"emitters", 1}, {"particles_per_emitter", 4}, {"bodies", 2}, {"frames", 3}});
  ARCANE_ASSERT_TRUE(doc.has_value());
  ARCANE_ASSERT_TRUE((*doc)["format"] == "arcane.bench" && (*doc)["version"] == 1);
  ARCANE_ASSERT_TRUE((*doc)["config"]["frames"] == 3 && (*doc)["results"]["live_particles"] == 4);
  for (const char* key : {"setup_ms", "total_ms", "avg_frame_ms", "max_frame_ms", "live_particles", "physics_bodies"}) {
    ARCANE_ASSERT_TRUE((*doc)["results"].contains(key));
  }
  auto bad_range = run_bench_json({{"frames", 0}});
  ARCANE_ASSERT_TRUE(!bad_range.has_value() && bad_range.error().code == "ARGS_INVALID");
  auto bad_key = run_bench_json({{"spirtes", 1}});
  ARCANE_ASSERT_TRUE(!bad_key.has_value() && bad_key.error().path == "/spirtes");
  auto bad_type = run_bench_json({{"frames", "ten"}});
  ARCANE_ASSERT_TRUE(!bad_type.has_value() && bad_type.error().path == "/frames");
}

void test_commands() {
  const fs::path dir = fs::temp_directory_path() / "arcane_debug_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  ComponentRegistry components;
  register_builtin_components(components);
  auto session = editor::ProjectSession::create(dir, "Debug", components);
  ARCANE_ASSERT_TRUE(session.has_value() && session->save(components).has_value());

  const std::string before = slurp(dir / "project.json");
  ARCANE_ASSERT_TRUE(before.find("\"debug\"") == std::string::npos);

  CommandRegistry registry;
  register_debug_commands(registry);
  register_bench_commands(registry);
  auto context = ProjectContext::open(dir);
  ARCANE_ASSERT_TRUE(context.has_value());
  ProjectContext& ctx = **context;

  auto got = registry.execute(ctx, {"debug.get", Json::object()});
  ARCANE_ASSERT_TRUE(got.has_value() && got->value["overlay_visible"] == false && got->value["overlay_font_scale"] == 1.5f);
  auto same = registry.execute(ctx, {"debug.set", {{"overlay_visible", false}}}, {.persist = true});
  ARCANE_ASSERT_TRUE(same.has_value() && same->value["changed"] == false && !same->inverse);
  ARCANE_ASSERT_TRUE(slurp(dir / "project.json") == before);

  auto set = registry.execute(ctx, {"debug.set", {{"overlay_visible", true}, {"overlay_font_scale", 2.0}}}, {.persist = true});
  ARCANE_ASSERT_TRUE(set.has_value() && set->value["changed"] == true && set->inverse);
  const std::string after = slurp(dir / "project.json");
  ARCANE_ASSERT_TRUE(canonicalize_json(after).value() == after);
  auto project = load_project_from_json_strict(after);
  ARCANE_ASSERT_TRUE(project.has_value() && project->debug.overlay_visible && project->debug.overlay_font_scale == 2.0f);
  ARCANE_ASSERT_TRUE(registry.execute(ctx, *set->inverse, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(slurp(dir / "project.json") == before);

  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"debug.set", {{"overlay_font_scale", 0}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"debug.set", {{"overlay_font_scale", 9}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"debug.set", {{"overlay_visible", "yes"}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(slurp(dir / "project.json") == before);

  auto bench = registry.execute(ctx, {"bench.run", {{"sprites", 5}, {"emitters", 0}, {"bodies", 1}, {"frames", 2}}});
  ARCANE_ASSERT_TRUE(bench.has_value() && bench->value["format"] == "arcane.bench" && !bench->inverse);
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"bench.run", {{"frames", 0}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"bench.run", {{"nope", 1}}}).has_value());

  fs::remove_all(dir, ec);
}

}  // namespace

int main() {
  test_overlay_data();
  test_benchmark();
  test_bench_json();
  test_commands();
  std::puts("test_debug OK");
  return 0;
}
