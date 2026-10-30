// Headless tests for the `arcane` CLI (AF4B): runs run_cli in process against temporary projects,
// including a compiled custom component (sample_game's Spinner) and a legacy sample copy, and the real
// executable for exit statuses. Covers JSON output, exit codes, selector ambiguity, dry-run, canonical
// output, reopen, batch, validate/fmt/migrate/schema and drift of docs/cli-reference.md.
#include <cstdlib>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "arcane/canonical_json.hpp"
#include "arcane_tooling/offline.hpp"
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
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
}

struct Out {
  int code = -1;
  Json json;
  std::string raw, err;
  std::string error_code() const { return json.contains("error") ? json["error"]["code"].get<std::string>() : ""; }
};

Out cli(std::vector<std::string> args, const std::string& stdin_text = {}) {
  std::istringstream in(stdin_text);
  std::ostringstream out, err;
  Out result;
  result.code = run_cli(args, in, out, err);
  result.raw = out.str();
  result.err = err.str();
  result.json = Json::parse(result.raw, nullptr, false);
  return result;
}

struct Project {
  fs::path dir;
  explicit Project(const char* name) : dir(fs::temp_directory_path() / name) { remove(); }
  ~Project() { remove(); }
  void remove() const {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  std::vector<std::string> base() const { return {"--project", dir.string(), "--module", ARCANE_SAMPLE_GAME_PATH}; }
  Out run(std::vector<std::string> words) const {
    auto args = base();
    args.insert(args.end(), words.begin(), words.end());
    return cli(args);
  }
  fs::path scene() const { return dir / "scenes" / "main.scene.json"; }
};

void test_project_flow() {
  Project p("arcane_cli_flow");
  auto made = cli({"project", "new", p.dir.string(), "--name", "Demo"});
  ARCANE_ASSERT_TRUE(made.code == 0 && made.json["ok"] == true && made.json["name"] == "Demo");
  ARCANE_ASSERT_TRUE(fs::is_regular_file(p.dir / "project.json") && fs::is_directory(p.dir / "assets"));
  ARCANE_ASSERT_TRUE(cli({"project", "new", p.dir.string()}).code == 5);  // never overwrites

  auto info = p.run({"project", "info"});
  ARCANE_ASSERT_TRUE(info.code == 0 && info.json["name"] == "Demo" && info.json["format"] == "v2" && info.json["scene_files"] == 1);

  auto player = p.run({"node", "add", "--name", "Player", "--position", "[10,20]"});
  ARCANE_ASSERT_TRUE(player.code == 0 && player.json["id"] == 1 && player.json["path"] == "/Player");
  auto spinner = p.run({"component", "add", "--node", "id:1", "--type", "Spinner"});
  ARCANE_ASSERT_TRUE(spinner.code == 0 && spinner.json["component_id"] == 2);

  auto set = p.run({"prop", "set", "--node", "path:/Player", "--component", "Spinner", "--name", "speed_degrees_per_second", "--value", "180.5"});
  ARCANE_ASSERT_TRUE(set.code == 0 && set.json["changed"] == true);
  ARCANE_ASSERT_TRUE(p.run({"prop", "set", "--node", "id:1", "--component", "2", "--name", "label", "--value", "\"hero\""}).code == 0);

  // Reopen in a fresh invocation: values persisted, file canonical.
  auto get = p.run({"prop", "get", "--node", "id:1", "--component", "Spinner", "--name", "speed_degrees_per_second"});
  ARCANE_ASSERT_TRUE(get.code == 0 && get.json["value"] == 180.5);
  const std::string text = slurp(p.scene());
  ARCANE_ASSERT_TRUE(arcane::canonicalize_json(text).value() == text);

  // Same value again is an idempotent no-op.
  auto again = p.run({"prop", "set", "--node", "id:1", "--component", "2", "--name", "speed_degrees_per_second", "--value", "180.5"});
  ARCANE_ASSERT_TRUE(again.code == 0 && again.json["changed"] == false);

  auto settings = p.run({"project", "settings", "get"});
  ARCANE_ASSERT_TRUE(settings.code == 0 && settings.json["main_scene"] == "scenes/main.scene.json");
  ARCANE_ASSERT_TRUE(p.run({"project", "settings-set", "--name", "Renamed"}).json["changed"] == true);
  ARCANE_ASSERT_TRUE(p.run({"project", "info"}).json["name"] == "Renamed");
  ARCANE_ASSERT_TRUE(p.run({"project", "settings-set", "--main-scene", "scenes/none.scene.json"}).code == 4);

  auto types = p.run({"component", "types"});
  ARCANE_ASSERT_TRUE(types.code == 0 && types.raw.find("Spinner") != std::string::npos);
}

void test_node_operations() {
  Project p("arcane_cli_nodes");
  ARCANE_ASSERT_TRUE(cli({"project", "new", p.dir.string()}).code == 0);
  p.run({"node", "add", "--name", "Root"});                                         // 1
  p.run({"node", "add", "--name", "A", "--parent", "id:1"});                        // 2
  p.run({"node", "add", "--name", "B", "--parent", "path:/Root"});                  // 3
  p.run({"component", "add", "--node", "id:2", "--type", "Camera2D"});              // 4
  p.run({"component", "add", "--node", "id:3", "--type", "Spinner"});               // 5

  // Duplicate A twice: both named A_copy -> a path selector is ambiguous, ids are not.
  ARCANE_ASSERT_TRUE(p.run({"node", "duplicate", "--node", "id:2"}).json["id"] == 6);
  ARCANE_ASSERT_TRUE(p.run({"node", "duplicate", "--node", "id:2"}).json["id"] == 8);
  auto ambiguous = p.run({"node", "info", "--node", "path:/Root/A_copy"});
  ARCANE_ASSERT_TRUE(ambiguous.code == 5 && ambiguous.error_code() == "SELECTOR_AMBIGUOUS");
  ARCANE_ASSERT_TRUE(p.run({"node", "info", "--node", "id:8"}).code == 0);
  ARCANE_ASSERT_TRUE(p.run({"node", "find", "--name-contains", "copy"}).json["count"] == 2);
  ARCANE_ASSERT_TRUE(p.run({"node", "find", "--component", "Camera2D"}).json["count"] == 3);

  // The Camera2D target is an entity reference: pointing it at B makes B unremovable.
  ARCANE_ASSERT_TRUE(p.run({"prop", "set", "--node", "id:2", "--component", "Camera2D", "--name", "target", "--value", "3"}).code == 0);
  auto blocked = p.run({"node", "remove", "--node", "id:3"});
  ARCANE_ASSERT_TRUE(blocked.code == 5 && blocked.error_code() == "CONFLICT");
  // Duplicating the camera keeps the external reference; duplicating a subtree remaps internal ones.
  ARCANE_ASSERT_TRUE(p.run({"node", "reparent", "--node", "id:3", "--parent", "id:2"}).code == 0);
  ARCANE_ASSERT_TRUE(p.run({"node", "reparent", "--node", "id:2", "--parent", "id:3"}).code == 4);  // cycle
  auto dup = p.run({"node", "duplicate", "--node", "id:2", "--name", "Sub"});
  ARCANE_ASSERT_TRUE(dup.code == 0 && dup.json["nodes"] == 2);
  const int new_camera = p.run({"node", "find", "--name", "Sub"}).json["nodes"][0]["id"].get<int>();
  const int new_child = p.run({"node", "find", "--under", "id:" + std::to_string(new_camera), "--name", "B"}).json["nodes"][0]["id"].get<int>();
  auto target = p.run({"prop", "get", "--node", "id:" + std::to_string(new_camera), "--component", "Camera2D", "--name", "target"});
  ARCANE_ASSERT_TRUE(target.json["value"] == new_child);

  ARCANE_ASSERT_TRUE(p.run({"node", "rename", "--node", "id:2", "--name", "has/slash"}).code == 4);
  ARCANE_ASSERT_TRUE(p.run({"node", "rename", "--node", "id:2", "--name", "Cam"}).code == 0);
  ARCANE_ASSERT_TRUE(p.run({"node", "set-transform", "--node", "path:/Root/Cam", "--position", "[3,4]"}).json["changed"] == true);
  ARCANE_ASSERT_TRUE(p.run({"component", "remove", "--node", "path:/Root/Cam", "--component", "Camera2D"}).code == 0);
  ARCANE_ASSERT_TRUE(p.run({"prop", "get", "--node", "id:2", "--component", "Camera2D", "--name", "zoom"}).code == 4);

  auto tree = p.run({"scene", "tree"});
  ARCANE_ASSERT_TRUE(tree.code == 0 && tree.json["nodes"].size() == 1 && tree.json["nodes"][0]["name"] == "Root");
  auto query = p.run({"scene", "query"});
  ARCANE_ASSERT_TRUE(query.json["nodes"] == 7 && query.json["component_types"]["Spinner"] == 2);

  // Remove a subtree (references to it are gone), ids are never reused afterwards.
  ARCANE_ASSERT_TRUE(p.run({"node", "remove", "--node", "id:2"}).json["nodes"] == 2);
  ARCANE_ASSERT_TRUE(p.run({"node", "info", "--node", "id:2"}).code == 4);
  auto fresh = p.run({"node", "add", "--name", "New"});
  ARCANE_ASSERT_TRUE(fresh.json["id"].get<int>() > 8);
}

void test_errors_and_dry_run() {
  Project p("arcane_cli_errors");
  ARCANE_ASSERT_TRUE(cli({"project", "new", p.dir.string()}).code == 0);
  p.run({"node", "add", "--name", "Twin"});
  p.run({"node", "add", "--name", "Twin"});
  p.run({"component", "add", "--node", "id:1", "--type", "Spinner"});

  ARCANE_ASSERT_TRUE(cli({"frobnicate"}).code == 2);
  ARCANE_ASSERT_TRUE(cli({}).code == 2);
  ARCANE_ASSERT_TRUE(cli({"--help"}).code == 0);
  ARCANE_ASSERT_TRUE(cli({"--project", (p.dir / "nope").string(), "scene", "tree"}).code == 3);
  ARCANE_ASSERT_TRUE(cli({"--project", p.dir.string(), "--module", (p.dir / "missing.so").string(), "scene", "tree"}).error_code() == "MODULE_LOAD_FAILED");
  ARCANE_ASSERT_TRUE(cli({"--project", p.dir.string(), "--module", (p.dir / "missing.so").string(), "scene", "tree"}).code == 6);
  ARCANE_ASSERT_TRUE(p.run({"node", "add"}).code == 2);                           // missing required argument
  ARCANE_ASSERT_TRUE(p.run({"node", "add", "--name", "X", "--bogus", "1"}).code == 2);
  ARCANE_ASSERT_TRUE(p.run({"node", "info", "--node", "path:/Twin"}).code == 5);   // ambiguous
  ARCANE_ASSERT_TRUE(p.run({"node", "info", "--node", "path:/Nope"}).code == 4);
  ARCANE_ASSERT_TRUE(p.run({"node", "info", "--node", "7"}).code == 2);            // bad selector
  ARCANE_ASSERT_TRUE(p.run({"prop", "set", "--node", "id:1", "--component", "Spinner", "--name", "nope", "--value", "1"}).error_code() == "PROPERTY_UNKNOWN");
  ARCANE_ASSERT_TRUE(p.run({"prop", "set", "--node", "id:1", "--component", "Spinner", "--name", "label", "--value", "5"}).error_code() == "VALUE_INVALID");
  ARCANE_ASSERT_TRUE(p.run({"prop", "set", "--node", "id:1", "--component", "Spinner", "--name", "label", "--value", "bare"}).code == 2);  // not JSON
  ARCANE_ASSERT_TRUE(p.run({"component", "add", "--node", "id:1", "--type", "Nope"}).error_code() == "COMPONENT_TYPE_UNKNOWN");

  // Dry run: reports the result, writes nothing.
  const std::string before = slurp(p.scene());
  auto dry = p.run({"prop", "set", "--node", "id:1", "--component", "Spinner", "--name", "label", "--value", "\"x\"", "--dry-run"});
  ARCANE_ASSERT_TRUE(dry.code == 0 && dry.json["dry_run"] == true && dry.json["changed"] == true);
  ARCANE_ASSERT_TRUE(p.run({"node", "remove", "--node", "id:1", "--dry-run"}).code == 0);
  ARCANE_ASSERT_TRUE(p.run({"node", "add", "--name", "Ghost", "--dry-run"}).code == 0);
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == before);
  ARCANE_ASSERT_TRUE(cli({"project", "new", (p.dir / "other").string(), "--dry-run"}).code == 0 && !fs::exists(p.dir / "other"));
  ARCANE_ASSERT_TRUE(p.run({"atlas", "pack", "--folder", "assets", "--atlas", "a.json", "--dry-run"}).code == 7);

  // A scene edited behind the CLI's back is a conflict only inside one process; across processes the
  // fresh read wins. The in-process check lives in test_tooling_commands.
  // A malformed scene refuses to open and is never rewritten.
  spit(p.scene(), "{ not json");
  ARCANE_ASSERT_TRUE(p.run({"node", "add", "--name", "Y"}).code == 4);
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == "{ not json");
}

void test_scenes_and_batch() {
  Project p("arcane_cli_batch");
  ARCANE_ASSERT_TRUE(cli({"project", "new", p.dir.string()}).code == 0);
  ARCANE_ASSERT_TRUE(p.run({"scene", "new", "scenes/level1.scene.json"}).json["scene"] == "scenes/level1.scene.json");
  ARCANE_ASSERT_TRUE(p.run({"scene", "new", "scenes/level1.scene.json"}).code == 5);
  ARCANE_ASSERT_TRUE(p.run({"scene", "new", "../escape.scene.json"}).code == 2);
  ARCANE_ASSERT_TRUE(p.run({"scene", "new", "scenes/bad.json"}).code == 2);
  auto list = p.run({"scene", "list"});
  ARCANE_ASSERT_TRUE(list.json["scenes"].size() == 2 && list.json["scenes"][0]["main"] == false);

  // Edit the second scene through --scene; the main scene is untouched.
  const std::string main_before = slurp(p.scene());
  ARCANE_ASSERT_TRUE(p.run({"--scene", "scenes/level1.scene.json", "node", "add", "--name", "Boss"}).code == 0);
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == main_before);
  ARCANE_ASSERT_TRUE(p.run({"--scene", "scenes/level1.scene.json", "node", "find", "--name", "Boss"}).json["count"] == 1);
  ARCANE_ASSERT_TRUE(p.run({"node", "find", "--name", "Boss"}).json["count"] == 0);

  const std::string script =
      "{\"command\":\"node.add\",\"args\":{\"name\":\"One\"}}\n"
      "\n"
      "{\"command\":\"node.add\",\"args\":{\"name\":\"Two\",\"parent\":\"path:/One\"}}\n"
      "{\"command\":\"component.add\",\"args\":{\"node\":\"id:2\",\"type\":\"Spinner\"}}\n"
      "{\"command\":\"prop.set\",\"args\":{\"node\":\"id:2\",\"component\":\"Spinner\",\"name\":\"label\",\"value\":\"batched\"}}\n";
  auto dry = cli({"--project", p.dir.string(), "--module", ARCANE_SAMPLE_GAME_PATH, "--dry-run", "batch", "-"}, script);
  ARCANE_ASSERT_TRUE(dry.code == 0 && dry.json["executed"] == 4 && dry.json["dry_run"] == true && dry.json["committed"] == false);
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == main_before);
  auto ran = cli({"--project", p.dir.string(), "--module", ARCANE_SAMPLE_GAME_PATH, "batch", "-"}, script);
  ARCANE_ASSERT_TRUE(ran.code == 0 && ran.json["executed"] == 4 && ran.json["committed"] == true);
  ARCANE_ASSERT_TRUE(p.run({"prop", "get", "--node", "path:/One/Two", "--component", "Spinner", "--name", "label"}).json["value"] == "batched");

  const std::string failing =
      "{\"command\":\"node.add\",\"args\":{\"name\":\"Kept\"}}\n"
      "{\"command\":\"node.remove\",\"args\":{\"node\":\"id:999\"}}\n"
      "{\"command\":\"node.add\",\"args\":{\"name\":\"NotRun\"}}\n";
  const std::string scene_before = slurp(p.scene());
  const std::string project_before = slurp(p.dir / "project.json");
  const std::vector<std::string> base = {"--project", p.dir.string(), "--module", ARCANE_SAMPLE_GAME_PATH, "batch", "-"};
  auto failed = cli(base, failing);
  ARCANE_ASSERT_TRUE(failed.code == 4 && failed.json["failed_line"] == 2 && failed.json["results"].size() == 1);
  ARCANE_ASSERT_TRUE(p.run({"node", "find", "--name", "Kept"}).json["count"] == 0);  // all-or-nothing
  ARCANE_ASSERT_TRUE(p.run({"node", "find", "--name", "NotRun"}).json["count"] == 0);
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == scene_before);

  // A project-level command and a scene edit commit together.
  auto both = cli(base,
                  "{\"command\":\"project.set\",\"args\":{\"name\":\"Renamed\"}}\n"
                  "{\"command\":\"node.add\",\"args\":{\"name\":\"Both\"}}\n");
  ARCANE_ASSERT_TRUE(both.code == 0 && both.json["committed"] == true);
  ARCANE_ASSERT_TRUE(slurp(p.dir / "project.json") != project_before && slurp(p.scene()) != scene_before);

  // An immediate-write command is refused up front; line 1 is not applied.
  const std::string scene_mid = slurp(p.scene());
  auto immediate = cli(base,
                       "{\"command\":\"node.add\",\"args\":{\"name\":\"Early\"}}\n"
                       "{\"command\":\"clip.create\",\"args\":{}}\n");
  ARCANE_ASSERT_TRUE(immediate.code == 7 && immediate.error_code() == "TRANSACTION_UNSUPPORTED" && immediate.json["failed_line"] == 2);
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == scene_mid);

  // A file-level mutating command is refused too.
  auto offline = cli(base, "{\"command\":\"scene.new\",\"args\":{\"path\":\"scenes/x.scene.json\"}}\n");
  ARCANE_ASSERT_TRUE(offline.code == 7 && offline.error_code() == "TRANSACTION_UNSUPPORTED");

  // Shape errors on any line stop everything before it runs.
  auto bad = cli(base, "not json\n{\"command\":\"node.add\",\"args\":{\"name\":\"Late\"}}\n");
  ARCANE_ASSERT_TRUE(bad.code == 2 && bad.json["failed_line"] == 1);
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == scene_mid);
}

void copy_sample(const fs::path& dest) {
  std::error_code ec;
  fs::remove_all(dest, ec);
  fs::copy(ARCANE_SAMPLE_PROJECT_DIR, dest, fs::copy_options::recursive);
}

void test_validate_fmt_migrate() {
  const fs::path dir = fs::temp_directory_path() / "arcane_cli_legacy";
  copy_sample(dir);
  auto args = [&](std::vector<std::string> words) {
    std::vector<std::string> all = {"--project", dir.string(), "--module", ARCANE_SAMPLE_GAME_PATH};
    all.insert(all.end(), words.begin(), words.end());
    return cli(all);
  };
  // The legacy sample opens and validates without migration; it is reported as needing migration.
  auto legacy = args({"validate"});
  ARCANE_ASSERT_TRUE(legacy.code == 0 && legacy.json["errors"] == 0 && legacy.json["warnings"].size() >= 2);
  ARCANE_ASSERT_TRUE(args({"project", "info"}).json["format"] == "legacy-v1");
  ARCANE_ASSERT_TRUE(args({"scene", "tree"}).code == 0);
  const std::string project_before = slurp(dir / "project.json");
  const std::string scene_before = slurp(dir / "scenes" / "main.scene.json");

  auto dry = args({"migrate", "--dry-run"});
  ARCANE_ASSERT_TRUE(dry.code == 0 && dry.json["changed"] == true && dry.json["applied"] == false);
  ARCANE_ASSERT_TRUE(slurp(dir / "project.json") == project_before);
  ARCANE_ASSERT_TRUE(args({"fmt"}).json["changed"] == false);  // legacy files are skipped, not rewritten
  ARCANE_ASSERT_TRUE(slurp(dir / "scenes" / "main.scene.json") == scene_before);

  auto done = args({"migrate"});
  ARCANE_ASSERT_TRUE(done.code == 0 && done.json["applied"] == true);
  ARCANE_ASSERT_TRUE(args({"migrate"}).json["changed"] == false);  // idempotent
  auto after = args({"validate"});
  ARCANE_ASSERT_TRUE(after.code == 0 && after.json["warnings"].empty());
  ARCANE_ASSERT_TRUE(args({"project", "info"}).json["format"] == "v2");

  // fmt: a hand-edited (non-canonical) v2 file is reported by --check, fixed by fmt.
  const fs::path scene = dir / "scenes" / "main.scene.json";
  const std::string canonical = slurp(scene);
  spit(scene, Json::parse(canonical).dump());
  ARCANE_ASSERT_TRUE(args({"validate"}).json["warnings"].size() == 1);
  auto check = args({"fmt", "--check"});
  ARCANE_ASSERT_TRUE(check.code == 4 && check.json["ok"] == false && slurp(scene) != canonical);
  ARCANE_ASSERT_TRUE(args({"fmt", "--dry-run"}).json["changed"] == true && slurp(scene) != canonical);
  ARCANE_ASSERT_TRUE(args({"fmt"}).code == 0 && slurp(scene) == canonical);
  ARCANE_ASSERT_TRUE(args({"fmt", "--check"}).code == 0);

  // Unknown data makes validate fail (exit 4) instead of being ignored.
  Json broken = Json::parse(canonical);
  broken["mystery"] = 1;
  spit(scene, broken.dump());
  ARCANE_ASSERT_TRUE(args({"validate"}).code == 4);
  ARCANE_ASSERT_TRUE(args({"validate", "--file", "scenes/main.scene.json"}).json["errors"] == 1);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// docs/commands.md is hand-written: every registered or offline command must be mentioned, and it must not
// name a command that does not exist.
void test_commands_doc_coverage() {
  const std::string doc = slurp(ARCANE_COMMANDS_DOC_PATH);
  ARCANE_ASSERT_TRUE(!doc.empty());
  CommandRegistry registry;
  register_all_commands(registry);
  std::set<std::string> names;
  for (const CommandSpec* spec : registry.list()) names.insert(spec->name);
  for (const auto& c : offline_commands()) names.insert(c.spec.name);
  std::set<std::string> groups;
  for (const auto& n : names) {
    const auto dot = n.find('.');
    if (dot != std::string::npos) groups.insert(n.substr(0, dot));
  }
  for (const auto& n : names) {
    const bool found = n.find('.') != std::string::npos
                           ? doc.find("`" + n + "`") != std::string::npos
                           : doc.find("`" + n + "`") != std::string::npos || doc.find("`arcane " + n) != std::string::npos;
    if (!found) {
      std::fprintf(stderr, "docs/commands.md does not mention %s\n", n.c_str());
      ARCANE_ASSERT_TRUE(false);
    }
  }
  const std::regex token("`([a-z_]+)\\.([a-z_]+)`");
  for (auto it = std::sregex_iterator(doc.begin(), doc.end(), token); it != std::sregex_iterator(); ++it) {
    const std::string group = (*it)[1], full = (*it)[0].str().substr(1, (*it)[0].length() - 2);
    const std::string ext = (*it)[2];
    if (ext == "json" || ext == "png" || ext == "md") continue;  // file names
    if (full == "save.data_version") continue;  // a project.json field, not a command
    if (!groups.count(group) || names.count(full)) continue;
    std::fprintf(stderr, "docs/commands.md names unknown command %s\n", full.c_str());
    ARCANE_ASSERT_TRUE(false);
  }
}

void test_schema_and_docs() {
  auto list = cli({"schema"});
  ARCANE_ASSERT_TRUE(list.code == 0 && list.raw.find("\"scene\"") != std::string::npos);
  auto scene = cli({"schema", "--type", "scene"});
  ARCANE_ASSERT_TRUE(scene.code == 0 && scene.json["schema"].is_object());
  ARCANE_ASSERT_TRUE(cli({"schema", "--type", "nope"}).code == 2);
  auto cmd = cli({"schema", "--command", "prop.set"});
  ARCANE_ASSERT_TRUE(cmd.code == 0 && cmd.json["mutating"] == true);
  auto spinner = cli({"--project", ARCANE_SAMPLE_PROJECT_DIR, "--module", ARCANE_SAMPLE_GAME_PATH, "schema", "--component", "Spinner"});
  ARCANE_ASSERT_TRUE(spinner.code == 0 && spinner.json["properties"].size() == 2);
  ARCANE_ASSERT_TRUE(cli({"--project", ARCANE_SAMPLE_PROJECT_DIR, "--module", ARCANE_SAMPLE_GAME_PATH, "schema", "--component", "Nope"}).code == 4);

  // docs/cli-reference.md is generated: regenerate with `arcane docs > docs/cli-reference.md`.
  auto docs = cli({"docs"});
  ARCANE_ASSERT_TRUE(docs.code == 0);
  if (docs.raw != slurp(ARCANE_CLI_REFERENCE_PATH)) {
    std::fprintf(stderr, "docs/cli-reference.md is out of date; run: arcane docs > docs/cli-reference.md\n");
    ARCANE_ASSERT_TRUE(false);
  }
  CommandRegistry registry;
  register_all_commands(registry);
  for (const CommandSpec* spec : registry.list()) {  // every command is documented
    ARCANE_ASSERT_TRUE(docs.raw.find("### `arcane ") != std::string::npos);
    std::string form = spec->name;
    const auto dot = form.find('.');
    if (dot != std::string::npos) form[dot] = ' ';
    for (char& c : form) if (c == '_') c = '-';
    ARCANE_ASSERT_TRUE(docs.raw.find("### `arcane " + form + "`") != std::string::npos);
  }
}

void test_run_and_test() {
  Project p("arcane_cli_run");
  ARCANE_ASSERT_TRUE(cli({"project", "new", p.dir.string()}).code == 0);
  p.run({"node", "add", "--name", "Player"});
  p.run({"component", "add", "--node", "id:1", "--type", "Spinner"});

  auto a = p.run({"run", "--frames", "30"});
  ARCANE_ASSERT_TRUE(a.code == 0 && a.json["frames"] == 30 && a.json["module"] == true && a.json["nodes"] == 1);
  ARCANE_ASSERT_TRUE(p.run({"run", "--frames", "30"}).json["final_hash"] == a.json["final_hash"]);
  ARCANE_ASSERT_TRUE(p.run({"run", "--frames", "0"}).code == 4 || p.run({"run", "--frames", "0"}).code == 2);

  const std::string scene_before = slurp(p.scene());
  ARCANE_ASSERT_TRUE(p.run({"--dry-run", "run", "--frames", "5", "--trace_out", "tests/x.trace.json"}).code == 0);
  ARCANE_ASSERT_TRUE(!fs::exists(p.dir / "tests" / "x.trace.json"));

  ARCANE_ASSERT_TRUE(p.run({"replay", "save", "--file", "tests/a.replay.json", "--frames", "20"}).code == 0);
  ARCANE_ASSERT_TRUE(p.run({"run", "--replay", "tests/a.replay.json", "--frames", "5"}).code == 2);
  auto traced = p.run({"run", "--replay", "tests/a.replay.json", "--trace_out", "out/a.trace.json"});
  ARCANE_ASSERT_TRUE(traced.code == 0 && traced.json["frames"] == 20 && traced.json["trace_out"] == "out/a.trace.json");
  ARCANE_ASSERT_TRUE(fs::is_regular_file(p.dir / "out" / "a.trace.json"));
  ARCANE_ASSERT_TRUE(slurp(p.scene()) == scene_before);  // run never saves

  auto missing = p.run({"test"});
  ARCANE_ASSERT_TRUE(missing.code == 4 && missing.json["ok"] == false && missing.json["failed"] == 1);
  ARCANE_ASSERT_TRUE(missing.json["cases"][0]["error"].get<std::string>().find("missing expected trace") == 0);

  fs::copy_file(p.dir / "out" / "a.trace.json", p.dir / "tests" / "a.trace.json");
  auto ok = p.run({"test"});
  ARCANE_ASSERT_TRUE(ok.code == 0 && ok.json["ok"] == true && ok.json["passed"] == 1 && ok.json["smoke"]["frames"] == 60);
  ARCANE_ASSERT_TRUE(p.run({"test", "--filter", "zzz"}).json["cases"].empty());

  std::string trace = slurp(p.dir / "tests" / "a.trace.json");
  const auto first = trace.find("\"hashes\"");
  const auto digit = trace.find('"', trace.find('[', first)) + 1;
  trace[digit] = trace[digit] == '0' ? '1' : '0';  // corrupt the first hash
  spit(p.dir / "tests" / "a.trace.json", trace);
  auto bad = p.run({"test"});
  ARCANE_ASSERT_TRUE(bad.code == 4 && bad.json["ok"] == false && bad.json["cases"][0]["first_divergent_frame"] == 0);
}

void test_export() {
  Project p("arcane_cli_export");
  ARCANE_ASSERT_TRUE(cli({"project", "new", p.dir.string()}).code == 0);
  p.run({"node", "add", "--name", "Player"});
  fs::create_directories(p.dir / "tests");
  spit(p.dir / "tests" / "x.txt", "not exported");
  const std::string out = (p.dir.parent_path() / "arcane_cli_export_out").string();
  fs::remove_all(out);

  auto dry = p.run({"--dry-run", "export", "--out", out});
  ARCANE_ASSERT_TRUE(dry.code == 0 && !fs::exists(out) && dry.json["module"] == fs::path(ARCANE_SAMPLE_GAME_PATH).filename().string() &&
                      dry.json["runtime_files"] == 0);
  auto done = p.run({"export", out});
  ARCANE_ASSERT_TRUE(done.code == 0 && done.json["files"] == dry.json["files"]);
  ARCANE_ASSERT_TRUE(fs::is_regular_file(out + "/game/project.json") && fs::is_regular_file(out + "/game/scenes/main.scene.json") &&
                      fs::is_regular_file(fs::path(out) / "game" / fs::path(ARCANE_SAMPLE_GAME_PATH).filename()));
  ARCANE_ASSERT_TRUE(!fs::exists(out + "/game/tests"));

  spit(out + "/keep.txt", "x");
  ARCANE_ASSERT_TRUE(p.run({"export", out}).code == 5 && fs::exists(out + "/keep.txt"));  // CONFLICT, untouched
  ARCANE_ASSERT_TRUE(p.run({"export", out, "--overwrite"}).code == 0 && !fs::exists(out + "/keep.txt"));
  ARCANE_ASSERT_TRUE(p.run({"export", p.dir.string(), "--overwrite"}).code == 2);  // out overlaps the project

  const std::string missing = out + "_missing";
  auto bad = cli({"--project", p.dir.string(), "--module", (p.dir / "nope.so").string(), "export", missing});
  ARCANE_ASSERT_TRUE(bad.code != 0 && !fs::exists(missing));
  fs::remove_all(out);
}

int exit_status(int raw) {
#ifdef _WIN32
  return raw;
#else
  return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
#endif
}

void test_executable() {
  const std::string exe = std::string("\"") + ARCANE_CLI_PATH + "\"";
  const std::string quiet =
#ifdef _WIN32
      " >NUL 2>&1";
#else
      " >/dev/null 2>&1";
#endif
  ARCANE_ASSERT_TRUE(exit_status(std::system((exe + " --help" + quiet).c_str())) == 0);
  ARCANE_ASSERT_TRUE(exit_status(std::system((exe + " frobnicate" + quiet).c_str())) == 2);
  ARCANE_ASSERT_TRUE(exit_status(std::system((exe + " --project arcane_no_such_dir scene tree" + quiet).c_str())) == 3);
#ifndef _WIN32
  // run needs no display: succeeds with DISPLAY and WAYLAND_DISPLAY unset.
  const fs::path dir = fs::temp_directory_path() / "arcane_cli_exe_run";
  std::error_code ec;
  fs::remove_all(dir, ec);
  ARCANE_ASSERT_TRUE(exit_status(std::system((exe + " project new \"" + dir.string() + "\"" + quiet).c_str())) == 0);
  unsetenv("DISPLAY");
  unsetenv("WAYLAND_DISPLAY");
  ARCANE_ASSERT_TRUE(exit_status(std::system((exe + " --project \"" + dir.string() + "\" run --frames 10" + quiet).c_str())) == 0);
  fs::remove_all(dir, ec);
#endif
}

}  // namespace

int main() {
  test_project_flow();
  test_node_operations();
  test_errors_and_dry_run();
  test_scenes_and_batch();
  test_validate_fmt_migrate();
  test_schema_and_docs();
  test_commands_doc_coverage();
  test_run_and_test();
  test_export();
  test_executable();
  return 0;
}
