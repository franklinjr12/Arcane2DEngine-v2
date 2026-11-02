// arcane: headless authoring CLI (docs/cli-reference.md).
#include <iostream>
#include <string>
#include <vector>

#include "arcane_tooling/offline.hpp"

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  return arcane::tooling::run_cli(args, std::cin, std::cout, std::cerr);
}
