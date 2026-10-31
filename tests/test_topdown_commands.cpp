// The top-down slice's module properties (ArenaRules) are reachable through the
// shared command registry: list, set with persist, dry run, rejection of bad
// input without writing, and reload of the edited copy.
#include <filesystem>
#include <fstream>
#include <sstream>

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

Invocation prop(const char* command, const char* name, Json value = nullptr) {
  Json args = {{"node", "path:/Arena"}, {"component", "ArenaRules"}};
  if (name) args["name"] = name;
  if (!value.is_null()) args["value"] = std::move(value);
  return {command, std::move(args)};
}

void run() {
  const fs::path dir = fs::temp_directory_path() / "arcane_topdown_commands";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::copy(ARCANE_TOPDOWN_PROJECT_DIR, dir, fs::copy_options::recursive, ec);
  ARCANE_ASSERT_TRUE(!ec);
  const fs::path scene = dir / "scenes" / "main.scene.json";
  const std::string original = slurp(scene);

  CommandRegistry registry;
  register_property_commands(registry);
  {
    auto context = ProjectContext::open(dir, ARCANE_TOPDOWN_MODULE_PATH);
    ARCANE_ASSERT_TRUE(context.has_value());

    auto listed = registry.execute(**context, prop("prop.list", nullptr));
    ARCANE_ASSERT_TRUE(listed.has_value());
    ARCANE_ASSERT_TRUE(listed->value["properties"].size() == 3);

    // Dry run and invalid calls leave the file alone.
    ARCANE_ASSERT_TRUE(registry.execute(**context, prop("prop.set", "player_speed", 200.0), {.dry_run = true}).has_value());
    ARCANE_ASSERT_TRUE(!registry.execute(**context, prop("prop.set", "no_such_property", 1.0), {.persist = true}).has_value());
    ARCANE_ASSERT_TRUE(!registry.execute(**context, prop("prop.set", "hit_damage", "many"), {.persist = true}).has_value());
    ARCANE_ASSERT_TRUE(slurp(scene) == original);

    auto set = registry.execute(**context, prop("prop.set", "player_speed", 200.0), {.persist = true});
    ARCANE_ASSERT_TRUE(set.has_value() && set->inverse.has_value());
  }
  {
    auto context = ProjectContext::open(dir, ARCANE_TOPDOWN_MODULE_PATH);
    ARCANE_ASSERT_TRUE(context.has_value());
    auto got = registry.execute(**context, prop("prop.get", "player_speed"));
    ARCANE_ASSERT_TRUE(got.has_value() && got->value["value"] == 200.0);
  }
  fs::remove_all(dir, ec);
}

}  // namespace

int main() { run(); }
