// The `arcane` command line: maps words and --flags onto registry and offline commands, runs JSONL
// batches, prints one JSON object per invocation (docs/cli-reference.md).
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "arcane_tooling/offline.hpp"

namespace arcane::tooling {

namespace {

struct Options {
  std::filesystem::path project_dir = ".";
  std::string module_override;
  std::string scene_override;
  bool dry_run = false;
  bool pretty = false;
  bool help = false;
};

ToolError usage_error(std::string message, std::string hint = "Run arcane --help or arcane docs") {
  return ToolError{"USAGE", std::move(message), "", "", std::move(hint)};
}

// Everything one invocation shares: options, the command tables and the lazily opened project.
class Session {
 public:
  Session(Options options, CommandRegistry& registry) : options_(std::move(options)), registry_(registry) {}

  const Options& options() const { return options_; }
  const CommandRegistry& registry() const { return registry_; }

  const CommandSpec* find_spec(const std::string& name) const {
    if (const auto* offline = find_offline_command(name)) return &offline->spec;
    return registry_.find(name);
  }

  // `memory_only` (dry-run batches): registry commands run and accumulate in memory, nothing is saved.
  std::expected<Json, ToolError> run(const std::string& name, const Json& args, bool memory_only = false) {
    if (const auto* offline = find_offline_command(name)) {
      if (auto valid = validate_args(offline->spec.args_schema, args); !valid) return std::unexpected(valid.error());
      OfflineEnv env{options_.project_dir, options_.module_override, options_.scene_override, &registry_};
      auto result = offline->run(env, args, options_.dry_run);
      if (result && offline->spec.mutating && !options_.dry_run) context_.reset();  // files changed under the open project
      return result;
    }
    const CommandSpec* spec = registry_.find(name);
    if (!spec) {
      return std::unexpected(ToolError{"UNKNOWN_COMMAND", "Unknown command '" + name + "'", "", "", "Run arcane docs for the command list"});
    }
    if (options_.dry_run && spec->immediate_write) {
      return std::unexpected(ToolError{"DRY_RUN_UNSUPPORTED", "'" + name + "' writes files immediately and has no dry-run", "", "",
                                       "Run it without --dry-run, or in a copy of the project"});
    }
    if (!context_) {
      auto opened = ProjectContext::open(options_.project_dir, options_.module_override, options_.scene_override);
      if (!opened) return std::unexpected(opened.error());
      context_ = std::move(*opened);
    }
    ExecuteOptions execute_options;
    execute_options.persist = !options_.dry_run && !memory_only;
    execute_options.dry_run = options_.dry_run && !memory_only;
    auto result = registry_.execute(*context_, Invocation{name, args}, execute_options);
    if (!result) return std::unexpected(result.error());
    if (result->value.is_object()) return result->value;
    return Json{{"value", result->value}};
  }

  // Writes the accumulated in-memory edits (scene and, when changed, project.json) as one transaction.
  // Returns whether anything was written.
  std::expected<bool, ToolError> commit() {
    if (!context_ || !(context_->dirty() || context_->project_dirty())) return false;
    if (auto saved = context_->save(); !saved) return std::unexpected(saved.error());
    return true;
  }

 private:
  Options options_;
  CommandRegistry& registry_;
  std::unique_ptr<ProjectContext> context_;  // destroyed with the session, before the registry
};

Json with_ok(Json result, bool dry_run) {
  Json out = Json::object();
  out["ok"] = true;
  if (result.is_object()) {
    for (auto it = result.begin(); it != result.end(); ++it) out[it.key()] = it.value();
  } else {
    out["value"] = std::move(result);
  }
  if (dry_run) out["dry_run"] = true;
  return out;
}

std::string render(const Json& json, bool pretty) { return pretty ? json.dump(2) : json.dump(); }

int fail(std::ostream& out, std::ostream& err, const ToolError& error, bool pretty) {
  out << render(error_json(error), pretty) << "\n";
  err << "arcane: " << error.code << ": " << error.message << "\n";
  return exit_code(error);
}

// "prop.set" -> "prop set", "project.settings_set" -> "project settings-set".
std::string cli_form(const std::string& name) {
  std::string out = name;
  const auto dot = out.find('.');
  if (dot != std::string::npos) out[dot] = ' ';
  std::replace(out.begin(), out.end(), '_', '-');
  return out;
}

std::string key_of(std::string flag) {
  std::replace(flag.begin(), flag.end(), '-', '_');
  return flag;
}

bool is_flag(const std::string& token) { return token.rfind("--", 0) == 0 && token.size() > 2; }

std::expected<Json, ToolError> convert_value(const std::string& key, const Json& property, const std::string& text) {
  const std::string type = property.value("type", "");
  if (type == "string") return Json(text);
  if (type == "boolean") {
    if (text == "true") return Json(true);
    if (text == "false") return Json(false);
    return std::unexpected(ToolError{"ARGS_INVALID", "--" + key + " expects true or false", "", "/" + key, ""});
  }
  Json parsed = Json::parse(text, nullptr, false);
  if (!parsed.is_discarded()) return parsed;
  if (property.value("cli_text", false)) return Json(text);
  return std::unexpected(ToolError{"ARGS_INVALID", "--" + key + " expects JSON (quote strings: '\"text\"'), got '" + text + "'", "", "/" + key,
                                   "Wrap the JSON in single quotes in the shell"});
}

struct Parsed {
  Options options;
  std::string command;  // empty when only options were given
  Json args = Json::object();
  std::vector<std::string> rest;  // positionals for `batch`
};

std::expected<Parsed, ToolError> parse(const std::vector<std::string>& tokens, const CommandRegistry& registry) {
  Parsed parsed;
  std::vector<std::string> words;
  std::size_t i = 0;
  auto take_global_value = [&](const std::string& flag, std::string& target) -> std::expected<void, ToolError> {
    if (i + 1 >= tokens.size()) return std::unexpected(usage_error(flag + " needs a value"));
    target = tokens[++i];
    return {};
  };
  // Pass 1: global options and command words, up to the first command flag.
  for (; i < tokens.size(); ++i) {
    const std::string& t = tokens[i];
    if (t == "--dry-run") parsed.options.dry_run = true;
    else if (t == "--json") continue;  // JSON is always the output; accepted for scripts written to the plan's examples
    else if (t == "--pretty") parsed.options.pretty = true;
    else if (t == "--help" || t == "-h") parsed.options.help = true;
    else if (words.empty() && (t == "--project" || t == "--module" || t == "--scene")) {
      std::string value;
      if (auto ok = take_global_value(t, value); !ok) return std::unexpected(ok.error());
      if (t == "--project") parsed.options.project_dir = value;
      else if (t == "--module") parsed.options.module_override = value;
      else parsed.options.scene_override = value;
    } else if (is_flag(t)) break;
    else words.push_back(t);
  }
  if (words.empty()) return parsed;

  // Resolve "group verb" (or "group verb-word" for names with an underscore) or a single word.
  auto exists = [&](const std::string& name) { return registry.find(name) || find_offline_command(name); };
  std::size_t used = 0;
  if (words.size() >= 2 && exists(words[0] + "." + key_of(words[1]))) {
    parsed.command = words[0] + "." + key_of(words[1]);
    used = 2;
  } else if (words.size() >= 3 && exists(words[0] + "." + key_of(words[1]) + "_" + key_of(words[2]))) {
    parsed.command = words[0] + "." + key_of(words[1]) + "_" + key_of(words[2]);
    used = 3;
  } else if (exists(words[0])) {
    parsed.command = words[0];
    used = 1;
  } else {
    return std::unexpected(ToolError{"UNKNOWN_COMMAND", "Unknown command '" + words[0] + (words.size() > 1 ? " " + words[1] : "") + "'", "", "",
                                     "Run arcane docs for the command list"});
  }
  parsed.rest.assign(words.begin() + used, words.end());
  const CommandSpec* spec = nullptr;
  if (const auto* offline = find_offline_command(parsed.command)) spec = &offline->spec;
  else spec = registry.find(parsed.command);
  const Json properties = spec->args_schema.value("properties", Json::object());

  // Positionals fill the schema's x-positional names in order.
  const Json positional = spec->args_schema.value("x-positional", Json::array());
  if (parsed.rest.size() > positional.size()) {
    return std::unexpected(usage_error("unexpected argument '" + parsed.rest[positional.size()] + "'"));
  }
  for (std::size_t p = 0; p < parsed.rest.size(); ++p) {
    const std::string key = positional[p].get<std::string>();
    auto value = convert_value(key, properties.value(key, Json::object()), parsed.rest[p]);
    if (!value) return std::unexpected(value.error());
    parsed.args[key] = *value;
  }

  // Pass 2: flags.
  for (; i < tokens.size(); ++i) {
    const std::string& t = tokens[i];
    if (t == "--dry-run") { parsed.options.dry_run = true; continue; }
    if (t == "--json") continue;
    if (t == "--pretty") { parsed.options.pretty = true; continue; }
    if (t == "--help" || t == "-h") { parsed.options.help = true; continue; }
    if (!is_flag(t)) return std::unexpected(usage_error("unexpected argument '" + t + "'"));
    std::string flag = t.substr(2);
    std::optional<std::string> inline_value;
    if (const auto eq = flag.find('='); eq != std::string::npos) {
      inline_value = flag.substr(eq + 1);
      flag = flag.substr(0, eq);
    }
    const std::string key = key_of(flag);
    const bool known = properties.contains(key);
    if (!known && (flag == "project" || flag == "module" || flag == "scene")) {
      if (!inline_value) {
        if (i + 1 >= tokens.size()) return std::unexpected(usage_error(t + " needs a value"));
        inline_value = tokens[++i];
      }
      if (flag == "project") parsed.options.project_dir = *inline_value;
      else if (flag == "module") parsed.options.module_override = *inline_value;
      else parsed.options.scene_override = *inline_value;
      continue;
    }
    if (flag == "args") {
      std::string text;
      if (inline_value) text = *inline_value;
      else if (i + 1 < tokens.size()) text = tokens[++i];
      Json object = Json::parse(text, nullptr, false);
      if (!object.is_object()) return std::unexpected(ToolError{"ARGS_INVALID", "--args expects a JSON object", "", "/args", ""});
      for (auto it = object.begin(); it != object.end(); ++it) parsed.args[it.key()] = it.value();
      continue;
    }
    const Json property = properties.value(key, Json::object());
    std::string text;
    if (inline_value) {
      text = *inline_value;
    } else if (property.value("type", "") == "boolean" && (i + 1 >= tokens.size() || is_flag(tokens[i + 1]) ||
                                                              (tokens[i + 1] != "true" && tokens[i + 1] != "false"))) {
      text = "true";
    } else if (i + 1 < tokens.size() && !is_flag(tokens[i + 1])) {
      text = tokens[++i];
    } else {
      return std::unexpected(ToolError{"ARGS_INVALID", "--" + flag + " needs a value", "", "/" + key, ""});
    }
    auto value = convert_value(key, property, text);
    if (!value) return std::unexpected(value.error());
    parsed.args[key] = *value;
  }
  return parsed;
}

const char* kUsage =
    "usage: arcane [--project DIR] [--module PATH] [--scene FILE] [--dry-run] [--pretty] <group> <verb> [args] [--name value ...]\n"
    "       arcane batch FILE|-       one JSON command per line: {\"command\":\"prop.set\",\"args\":{...}}\n"
    "       arcane docs               the generated command reference (docs/cli-reference.md)\n"
    "Prints one JSON object on stdout; exit status 0 ok, 2 usage, 3 file/project, 4 invalid, 5 conflict,\n"
    "6 module, 7 unsupported (including TRANSACTION_UNSUPPORTED in a batch).\n";

int run_batch(Session& session, const std::string& source, std::istream& in, std::ostream& out, std::ostream& err) {
  std::string text;
  if (source == "-") {
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
  } else {
    std::ifstream file(source, std::ios::binary);
    if (!file) return fail(out, err, ToolError{"IO_ERROR", "cannot read batch file '" + source + "'", source, "", ""}, session.options().pretty);
    std::ostringstream ss;
    ss << file.rdbuf();
    text = ss.str();
  }
  struct Line {
    int number;
    std::string command;
    Json args;
  };
  const bool pretty = session.options().pretty;
  Json results = Json::array();
  auto stop = [&](int number, const ToolError& error) {
    Json body = error_json(error);
    body["failed_line"] = number > 0 ? Json(number) : Json(nullptr);
    body["results"] = results;
    out << render(body, pretty) << "\n";
    err << "arcane: " << (number > 0 ? "line " + std::to_string(number) + ": " : std::string()) << error.code << ": " << error.message << "\n";
    return exit_code(error);
  };

  // Pass 1: check every line before anything runs.
  std::vector<Line> script;
  std::istringstream lines(text);
  std::string line;
  int number = 0;
  while (std::getline(lines, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    Json call = Json::parse(line, nullptr, false);
    if (!call.is_object() || !call.contains("command") || !call["command"].is_string() ||
        (call.contains("args") && !call["args"].is_object())) {
      return stop(number, ToolError{"ARGS_INVALID", "each line must be {\"command\": string, \"args\": object}", "", "", ""});
    }
    const std::string name = call["command"].get<std::string>();
    const bool offline = find_offline_command(name) != nullptr;
    const CommandSpec* spec = session.find_spec(name);
    if (!spec) {
      return stop(number, ToolError{"UNKNOWN_COMMAND", "Unknown command '" + name + "'", "", "", "Run arcane docs for the command list"});
    }
    if ((offline && spec->mutating) || (!offline && spec->immediate_write)) {
      return stop(number, ToolError{"TRANSACTION_UNSUPPORTED", "'" + name + "' writes files outside the batch transaction", "", "",
                                    "Run it as its own arcane invocation"});
    }
    script.push_back({number, name, call.value("args", Json::object())});
  }

  // Pass 2: run in memory; nothing is written until every line succeeded.
  for (const Line& item : script) {
    auto result = session.run(item.command, item.args, /*memory_only=*/true);
    if (!result) return stop(item.number, result.error());
    if (result->is_object() && result->contains("ok") && (*result)["ok"] == false) {
      results.push_back({{"line", item.number}, {"ok", false}, {"result", *result}});
      Json body = {{"ok", false}, {"failed_line", item.number}, {"results", results}};
      out << render(body, pretty) << "\n";
      return 4;
    }
    results.push_back({{"line", item.number}, {"ok", true}, {"result", *result}});
  }
  bool committed = false;
  if (!session.options().dry_run) {
    auto saved = session.commit();
    if (!saved) return stop(0, saved.error());
    committed = *saved;
  }
  Json body = {{"ok", true}, {"executed", results.size()}, {"committed", committed}, {"results", results}};
  if (session.options().dry_run) body["dry_run"] = true;
  out << render(body, pretty) << "\n";
  return 0;
}

}  // namespace

int run_cli(const std::vector<std::string>& args, std::istream& in, std::ostream& out, std::ostream& err) {
  CommandRegistry registry;
  register_all_commands(registry);

  // `batch` takes a file operand that parse() would treat as a command word; handle it first.
  std::vector<std::string> tokens = args;
  std::string batch_source;
  bool is_batch = false;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "batch" && (i == 0 || tokens[i - 1] == "--dry-run" || tokens[i - 1] == "--json" || tokens[i - 1] == "--pretty" ||
                                 (i >= 2 && (tokens[i - 2] == "--project" || tokens[i - 2] == "--module" || tokens[i - 2] == "--scene")))) {
      if (i + 1 >= tokens.size()) {
        return fail(out, err, usage_error("batch needs a file operand (or - for stdin)"), false);
      }
      batch_source = tokens[i + 1];
      tokens.erase(tokens.begin() + static_cast<long>(i), tokens.begin() + static_cast<long>(i) + 2);
      is_batch = true;
      break;
    }
  }

  auto parsed = parse(tokens, registry);
  if (!parsed) return fail(out, err, parsed.error(), false);
  if (parsed->options.help || (parsed->command.empty() && !is_batch)) {
    out << kUsage;
    return parsed->options.help ? 0 : 2;
  }
  Session session(parsed->options, registry);
  const bool pretty = parsed->options.pretty;
  if (is_batch) return run_batch(session, batch_source, in, out, err);

  auto result = session.run(parsed->command, parsed->args);
  if (!result) return fail(out, err, result.error(), pretty);
  if (parsed->command == "docs" && result->is_object() && result->contains("text")) {
    out << (*result)["text"].get<std::string>();  // raw markdown, the one non-JSON output
    return 0;
  }
  const bool ok = !(result->is_object() && result->contains("ok") && (*result)["ok"] == false);
  Json body = with_ok(std::move(*result), parsed->options.dry_run);
  out << render(body, pretty) << "\n";
  return ok ? 0 : 4;
}

// ---- generated reference ---------------------------------------------------------------------

namespace {

std::string type_label(const Json& property) {
  if (property.value("cli_text", false)) return "id or name";
  const std::string type = property.value("type", "");
  return type.empty() ? "json" : type;
}

void describe(std::ostringstream& md, const CommandSpec& spec) {
  md << "### `arcane " << cli_form(spec.name) << "`\n\n" << spec.summary << "\n\n";
  if (find_offline_command(spec.name)) {
    md << (spec.mutating ? "Writes files; honors `--dry-run`.\n\n" : "Read only.\n\n");
  } else if (spec.immediate_write) {
    md << "Writes files immediately; `--dry-run` is refused (`DRY_RUN_UNSUPPORTED`).\n\n";
  } else if (spec.mutating) {
    md << "Edits the open scene (hud.* edit project.json) and saves each file atomically; `--dry-run` shows the result and saves nothing.\n\n";
  } else {
    md << "Read only.\n\n";
  }
  const Json properties = spec.args_schema.value("properties", Json::object());
  if (properties.empty()) {
    md << "No arguments.\n\n";
    return;
  }
  const Json required = spec.args_schema.value("required", Json::array());
  const Json positional = spec.args_schema.value("x-positional", Json::array());
  md << "| Argument | Type | Required | Positional |\n| --- | --- | --- | --- |\n";
  for (auto it = properties.begin(); it != properties.end(); ++it) {
    const bool is_required = std::find(required.begin(), required.end(), it.key()) != required.end();
    const auto pos = std::find(positional.begin(), positional.end(), it.key());
    std::string flag = it.key();
    std::replace(flag.begin(), flag.end(), '_', '-');
    md << "| `--" << flag << "` | " << type_label(it.value()) << " | " << (is_required ? "yes" : "no") << " | "
       << (pos == positional.end() ? "" : std::to_string(pos - positional.begin() + 1)) << " |\n";
  }
  md << "\n";
}

}  // namespace

std::string generate_cli_reference(const CommandRegistry& registry) {
  std::ostringstream md;
  md << "# `arcane` command reference\n\n"
        "<!-- Generated by `arcane docs`; do not edit. tests/test_cli.cpp fails when this file drifts. -->\n\n"
        "`arcane` is the headless authoring command line. It links the same command registry as the tests and\n"
        "has no window, GL or audio dependency. Nothing starts a server.\n\n"
        "## Usage\n\n"
        "```\narcane [--project DIR] [--module PATH] [--scene FILE] [--dry-run] [--pretty] <group> <verb> [args]\n"
        "arcane batch FILE|-\narcane docs\n```\n\n"
        "- `--project DIR` is the project folder (default `.`); `--module PATH` replaces the project's native module\n"
        "  (used by build-tree tests); `--scene FILE` edits another scene than `main_scene`.\n"
        "- Arguments are `--name value` (dashes become underscores) or `--name=value`; the listed positional\n"
        "  arguments may be given without a flag. Strings are taken as text; every other argument and every\n"
        "  untyped one (`--value`) is JSON, so quote it for the shell: `--value '180.0'`, `--value '\"text\"'`,\n"
        "  `--value '[1,2]'`. `--args '{...}'` supplies several arguments as one JSON object. Booleans may be bare\n"
        "  (`--overwrite`). Verbs with an underscore are written with a dash: `project settings-set`\n"
        "  (also accepted as `project settings set`).\n"
        "- `--json` is accepted and changes nothing: the output is always JSON. `--pretty` indents it.\n"
        "- `--dry-run` runs everything and writes nothing (commands that write files immediately refuse it).\n\n"
        "## Output and exit status\n\n"
        "One JSON object on stdout: `{\"ok\":true, ...result}` or\n"
        "`{\"ok\":false,\"error\":{\"code\",\"message\",\"file\",\"path\",\"hint\"}}` (absent fields are null). A one-line\n"
        "diagnostic goes to stderr. `arcane docs` prints this reference as raw Markdown.\n\n"
        "| Exit | Meaning | Codes |\n| --- | --- | --- |\n"
        "| 0 | Success, including no-op | |\n"
        "| 1 | Other failure | `AUDIO_UNAVAILABLE` |\n"
        "| 2 | Usage or argument error | `USAGE`, `UNKNOWN_COMMAND`, `ARGS_INVALID` |\n"
        "| 3 | File/IO or project not found | `PROJECT_NOT_FOUND`, `IO_ERROR` |\n"
        "| 4 | Schema, reference or validation error | `SCHEMA_INVALID`, `VALUE_INVALID`, `PROPERTY_UNKNOWN`, `NODE_NOT_FOUND`, "
        "`COMPONENT_NOT_FOUND`, `COMPONENT_TYPE_UNKNOWN`, `REPLAY_DIVERGED`; `validate` with problems; `fmt --check` with differences |\n"
        "| 5 | Conflict, ambiguous selector, concurrent edit | `CONFLICT`, `SELECTOR_AMBIGUOUS` |\n"
        "| 6 | Module load or execution failure | `MODULE_LOAD_FAILED`, `MODULE_FAILURE` |\n"
        "| 7 | Unsupported version or feature | `UNSUPPORTED_VERSION`, `DRY_RUN_UNSUPPORTED`, `TRANSACTION_UNSUPPORTED` |\n\n"
        "## Selectors\n\n"
        "Node: `id:42` (preferred, stable) or `path:/Root/Player` (names must be unique at each level, otherwise\n"
        "`SELECTOR_AMBIGUOUS`). Component: its integer id, or a type name attached to the node exactly once.\n\n"
        "## Writes and safety\n\n"
        "Every scene command saves the scene file by writing a temporary file next to it and renaming it, after\n"
        "serializing and re-validating in memory; a failure leaves the old file. If the scene file changed on disk\n"
        "since it was read the command fails with `CONFLICT` (an edit check, not a lock). The CLI keeps no undo\n"
        "history; use git. Atomicity is per command; `batch` is atomic for the whole script (see below).\n\n"
        "## Batch\n\n"
        "`arcane batch FILE|-` reads one JSON object per line, `{\"command\":\"prop.set\",\"args\":{...}}` (command\n"
        "names use dots; blank lines are skipped) and either commits every line or writes nothing:\n\n"
        "1. Validate all: every line must have the right shape (`ARGS_INVALID`) and a known command\n"
        "   (`UNKNOWN_COMMAND`). Commands that write files outside the transaction are refused with\n"
        "   `TRANSACTION_UNSUPPORTED` (exit 7): the ones marked as writing files immediately (`clip.*`,\n"
        "   `transition.set`, `atlas.pack`, `prefab.save`, ...) and file-level commands that change files\n"
        "   (`scene.new`, `migrate`, `fmt`, `project.new`, ...). Nothing has run when this fails.\n"
        "2. Run in memory: the lines run in order against one open project and the edits accumulate. Read-only\n"
        "   file-level commands (`scene.list`, `validate`, ...) are allowed but see the files on disk, not the\n"
        "   uncommitted edits. The first failing line stops the run; nothing is saved, and the reply has\n"
        "   `failed_line`, the error and the results so far.\n"
        "3. Commit: after every line succeeded, the changed files (the scene and, if a project-level command\n"
        "   ran, `project.json`) are written in one transactional save. If that fails the reply has the error,\n"
        "   `\"failed_line\": null` and the per-line `results`, with the files unchanged.\n\n"
        "The success reply has `\"committed\"`: true when files were written, false for `--dry-run` or when nothing\n"
        "changed. With `--dry-run` the commands run cumulatively in memory and nothing is saved.\n\n"
        "## Coverage\n\n"
        "Covered: project, scene files, nodes, components, typed properties, prefabs, atlases, transitions, saves,\n"
        "replay, bench, audio probe, validate, fmt, migrate, schema, docs, batch. Not available (no command yet):\n"
        "tilemap/tileset editing, HUD, input maps, animation clip authoring beyond `atlas make-clip`, physics\n"
        "authoring beyond properties, `run`, `export`, `render`, `test`. The editor does not use\n"
        "these commands for its edits yet (docs/editor-command-inventory.md).\n\n"
        "## Commands\n\n";
  std::map<std::string, const CommandSpec*> all;
  for (const CommandSpec* spec : registry.list()) all[spec->name] = spec;
  for (const auto& offline : offline_commands()) all[offline.spec.name] = &offline.spec;
  for (const auto& [name, spec] : all) describe(md, *spec);
  return md.str();
}

}  // namespace arcane::tooling
