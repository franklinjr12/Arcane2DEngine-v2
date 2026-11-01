// arcane_bench: headless benchmark, same code and JSON as the bench.run command (docs/perf.md).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "arcane_tooling/command.hpp"

namespace {

int usage(const char* message) {
  if (message) std::fprintf(stderr, "arcane_bench: %s\n", message);
  std::fprintf(stderr,
               "usage: arcane_bench [--sprites N] [--emitters N] [--particles-per-emitter N] [--bodies N] "
               "[--frames N]\nPrints an arcane.bench v1 JSON document on stdout. Exit 2 on bad arguments.\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  arcane::tooling::Json args = arcane::tooling::Json::object();
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
    args[key] = n;
  }
  auto doc = arcane::tooling::run_bench_json(args);
  if (!doc) {
    std::fprintf(stderr, "arcane_bench: %s: %s\n", doc.error().code.c_str(), doc.error().message.c_str());
    return 2;
  }
  std::printf("%s\n", doc->dump(2).c_str());
  return 0;
}
