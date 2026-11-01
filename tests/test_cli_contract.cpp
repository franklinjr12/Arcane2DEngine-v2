// CLI error contract (IN5): every registered and offline command is run with an invalid argument against a
// copy of the sample project. Each must exit 2 with the documented JSON error shape on stdout, a one-line
// diagnostic on stderr, and leave every file of the project byte-identical. The command list is generated
// from the registry, so new commands are covered automatically. Also checks that each exit code in the
// table of docs/cli-reference.md is the one error_code() maps its codes to.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#include "arcane_tooling/offline.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane::tooling;
namespace fs = std::filesystem;

std::map<std::string, std::string> snapshot(const fs::path& dir) {
  std::map<std::string, std::string> files;
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    files[fs::relative(entry.path(), dir).generic_string()] = ss.str();
  }
  return files;
}

// Arguments that the schema must reject, or null when the schema accepts every object.
Json invalid_args(const CommandSpec& spec) {
  const Json& schema = spec.args_schema;
  if (schema.contains("required") && !schema["required"].empty()) return Json::object();  // missing required
  if (!schema.value("additionalProperties", true)) return Json{{"__not_an_argument__", 1}};
  const Json properties = schema.value("properties", Json::object());
  for (auto it = properties.begin(); it != properties.end(); ++it) {
    const std::string type = it.value().value("type", "");
    if (type.empty()) continue;
    // A string is the wrong type for everything else, an object for a string.
    return Json{{it.key(), type == "string" ? Json::object() : Json("not-a-" + type)}};
  }
  return nullptr;
}

std::string cli_form(std::string name) {
  const auto dot = name.find('.');
  if (dot != std::string::npos) name[dot] = ' ';
  std::replace(name.begin(), name.end(), '_', '-');
  return name;
}

void test_every_command_rejects_invalid_arguments() {
  CommandRegistry registry;
  register_all_commands(registry);
  std::vector<const CommandSpec*> specs = registry.list();
  for (const auto& c : offline_commands()) specs.push_back(&c.spec);
  ARCANE_ASSERT_TRUE(specs.size() > 100);

  const fs::path dir = fs::temp_directory_path() / "arcane_cli_contract";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::copy(ARCANE_SAMPLE_PROJECT_DIR, dir, fs::copy_options::recursive);
  const auto before = snapshot(dir);

  std::set<std::string> uncovered;
  int covered = 0;
  for (const CommandSpec* spec : specs) {
    const Json args = invalid_args(*spec);
    if (args.is_null()) {
      uncovered.insert(spec->name);
      continue;
    }
    std::string words = cli_form(spec->name);
    std::vector<std::string> tokens = {"--project", dir.string(), "--module", ARCANE_SAMPLE_GAME_PATH};
    std::istringstream split(words);
    for (std::string w; split >> w;) tokens.push_back(w);
    tokens.push_back("--args");
    tokens.push_back(args.dump());

    std::istringstream in;
    std::ostringstream out, err;
    const int code = run_cli(tokens, in, out, err);
    const Json body = Json::parse(out.str(), nullptr, false);
    const std::string label = spec->name + " " + args.dump();
    if (code != 2) std::cerr << "FAIL " << label << ": exit " << code << " output " << out.str() << "\n";
    ARCANE_ASSERT_TRUE(code == 2);
    ARCANE_ASSERT_TRUE(body.is_object() && body["ok"] == false && body["error"].is_object());
    for (const char* key : {"code", "message", "file", "path", "hint"}) ARCANE_ASSERT_TRUE(body["error"].contains(key));
    ARCANE_ASSERT_TRUE(body["error"]["code"] == "ARGS_INVALID" && body["error"]["message"].is_string());
    ARCANE_ASSERT_TRUE(out.str().find('\n') == out.str().size() - 1);  // one object, one line
    ARCANE_ASSERT_TRUE(err.str().rfind("arcane: ARGS_INVALID: ", 0) == 0);
    if (snapshot(dir) != before) std::cerr << "FAIL " << label << ": project files changed\n";
    ARCANE_ASSERT_TRUE(snapshot(dir) == before);
    ++covered;
  }
  // Commands whose schema accepts any object cannot be given an invalid argument; keep the list visible.
  for (const auto& name : uncovered) std::cout << "no invalid argument possible: " << name << "\n";
  ARCANE_ASSERT_TRUE(covered > 100);
  fs::remove_all(dir, ec);
}

void test_unknown_command_and_usage() {
  auto run = [](std::vector<std::string> tokens, std::string& out_text) {
    std::istringstream in;
    std::ostringstream out, err;
    const int code = run_cli(tokens, in, out, err);
    out_text = out.str();
    return code;
  };
  std::string text;
  ARCANE_ASSERT_TRUE(run({"no", "such"}, text) == 2);
  Json body = Json::parse(text, nullptr, false);
  ARCANE_ASSERT_TRUE(body["ok"] == false && body["error"]["code"] == "UNKNOWN_COMMAND");
  ARCANE_ASSERT_TRUE(run({"--project"}, text) == 2);
  ARCANE_ASSERT_TRUE(Json::parse(text, nullptr, false)["error"]["code"] == "USAGE");
  ARCANE_ASSERT_TRUE(run({"--project", "/nonexistent/arcane_project", "info"}, text) != 0);
}

void test_exit_code_table() {
  const std::map<std::string, int> expected = {
      {"USAGE", 2}, {"UNKNOWN_COMMAND", 2}, {"ARGS_INVALID", 2}, {"PROJECT_NOT_FOUND", 3}, {"IO_ERROR", 3},
      {"SCHEMA_INVALID", 4}, {"VALUE_INVALID", 4}, {"PROPERTY_UNKNOWN", 4}, {"NODE_NOT_FOUND", 4},
      {"COMPONENT_NOT_FOUND", 4}, {"COMPONENT_TYPE_UNKNOWN", 4}, {"REPLAY_DIVERGED", 4}, {"CONFLICT", 5},
      {"SELECTOR_AMBIGUOUS", 5}, {"MODULE_LOAD_FAILED", 6}, {"MODULE_FAILURE", 6}, {"UNSUPPORTED_VERSION", 7},
      {"DRY_RUN_UNSUPPORTED", 7}, {"TRANSACTION_UNSUPPORTED", 7}, {"RENDER_UNAVAILABLE", 8}, {"AUDIO_UNAVAILABLE", 1},
      {"SOMETHING_ELSE", 1}};
  for (const auto& [code, exit] : expected) ARCANE_ASSERT_TRUE(exit_code(ToolError{code, "", "", "", ""}) == exit);
  // The generated reference lists every code under its exit status.
  std::ifstream in(ARCANE_CLI_REFERENCE_PATH);
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string doc = ss.str();
  for (const auto& [code, exit] : expected) {
    if (code == "SOMETHING_ELSE") continue;
    const auto row = doc.find("| " + std::to_string(exit) + " | ");
    const auto end = doc.find('\n', row);
    ARCANE_ASSERT_TRUE(row != std::string::npos);
    ARCANE_ASSERT_TRUE(doc.substr(row, end - row).find("`" + code + "`") != std::string::npos);
  }
}

}  // namespace

int main() {
  test_every_command_rejects_invalid_arguments();
  test_unknown_command_and_usage();
  test_exit_code_table();
  return 0;
}
