// arcane_bench: headless benchmark, same code and JSON as the bench.run command (docs/perf.md).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "arcane_editor/render_bench.hpp"
#include "arcane_tooling/command.hpp"

namespace {

int usage(const char* message) {
  if (message) std::fprintf(stderr, "arcane_bench: %s\n", message);
  std::fprintf(stderr,
               "usage: arcane_bench [--sprites N] [--emitters N] [--particles-per-emitter N] [--bodies N] "
               "[--frames N] [--render 0|1]\nPrints an arcane.bench v1 JSON document on stdout. Exit 2 on bad arguments. "
               "--render 1 also renders the scene offscreen (1280x720, needs a display or xvfb-run) and adds a "
               "\"render\" object; without a display it exits 8.\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  arcane::tooling::Json args = arcane::tooling::Json::object();
  bool render = false;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--help" || flag == "-h") return usage(nullptr) - 2;
    if (flag.rfind("--", 0) != 0 || i + 1 >= argc) return usage(("bad argument '" + flag + "'").c_str());
    std::string key = flag.substr(2);
    for (char& c : key) if (c == '-') c = '_';
    char* end = nullptr;
    const std::string value = argv[++i];
    const long n = std::strtol(value.c_str(), &end, 10);
    if (value.empty() || *end != '\0') return usage(("'" + value + "' is not an integer").c_str());
    if (key == "render") {
      if (n != 0 && n != 1) return usage("--render must be 0 or 1");
      render = n == 1;
      continue;
    }
    args[key] = n;
  }
  auto doc = arcane::tooling::run_bench_json(args);
  if (!doc) {
    std::fprintf(stderr, "arcane_bench: %s: %s\n", doc.error().code.c_str(), doc.error().message.c_str());
    return 2;
  }
  if (render) {
    const arcane::BenchConfig config{.sprites = (*doc)["config"]["sprites"].get<int>(),
                                     .emitters = (*doc)["config"]["emitters"].get<int>(),
                                     .particles_per_emitter = (*doc)["config"]["particles_per_emitter"].get<int>(),
                                     .bodies = (*doc)["config"]["bodies"].get<int>(),
                                     .frames = (*doc)["config"]["frames"].get<int>()};
    auto result = arcane::editor::run_render_benchmark(config);
    if (!result) {
      std::fprintf(stderr, "arcane_bench: RENDER_UNAVAILABLE: %s\n", result.error().c_str());
      return 8;
    }
    (*doc)["render"] = {{"width", result->width},
                        {"height", result->height},
                        {"render_avg_frame_ms", result->render_avg_frame_ms},
                        {"render_max_frame_ms", result->render_max_frame_ms},
                        {"update_avg_ms", result->update_avg_ms},
                        {"draw_calls", result->draw_calls},
                        {"sprites", result->sprites},
                        {"culled", result->culled},
                        {"live_particles", result->live_particles},
                        {"physics_bodies", result->physics_bodies}};
  }
  std::printf("%s\n", doc->dump(2).c_str());
  return 0;
}
