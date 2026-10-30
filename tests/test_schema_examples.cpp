// Schema/format drift gate: every schemas/*.schema.json is canonical, uses only keywords the small validator
// below understands, and validates the real canonical files that carry its `format` (samples/, tests/golden/),
// including the minimal examples in tests/golden/schema_examples/, which the engine readers must also accept.
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "arcane/atlas.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/prefab.hpp"
#include "arcane/replay.hpp"
#include "arcane/save_game.hpp"
#include "arcane_tooling/error.hpp"
#include "test_assert.hpp"

namespace {

using arcane::tooling::Json;
namespace fs = std::filesystem;

// Keywords with an effect (checked) and annotations (ignored). Anything else in a schema fails the test.
const std::set<std::string> kChecked = {"type",     "const",    "enum",     "required",  "properties",
                                        "additionalProperties", "items",    "minItems", "maxItems",  "minimum",
                                        "maximum",  "exclusiveMinimum",     "minLength", "pattern",   "oneOf",
                                        "$ref",     "propertyNames"};
const std::set<std::string> kIgnored = {"$defs", "$id", "$schema", "title", "description"};

std::string read(const fs::path& path) {
  auto text = arcane::read_text_file(path);
  if (!text) {
    std::fprintf(stderr, "cannot read %s: %s\n", path.string().c_str(), text.error().c_str());
    std::exit(1);
  }
  return *text;
}

Json parse(const std::string& text, const std::string& what) {
  Json json = Json::parse(text, nullptr, false);
  if (json.is_discarded()) {
    std::fprintf(stderr, "invalid JSON: %s\n", what.c_str());
    std::exit(1);
  }
  return json;
}

// Fails the test on a schema keyword the validator would silently ignore.
void lint_schema(const Json& schema, const std::string& where, std::vector<std::string>& problems) {
  if (!schema.is_object()) return;
  for (const auto& [key, value] : schema.items()) {
    if (!kChecked.count(key) && !kIgnored.count(key)) {
      problems.push_back(where + ": unsupported schema keyword '" + key + "'");
    }
  }
  if (schema.contains("properties")) {
    for (const auto& [name, sub] : schema["properties"].items()) lint_schema(sub, where + "/properties/" + name, problems);
  }
  if (schema.contains("$defs")) {
    for (const auto& [name, sub] : schema["$defs"].items()) lint_schema(sub, where + "/$defs/" + name, problems);
  }
  for (const char* key : {"items", "additionalProperties", "propertyNames"}) {
    if (schema.contains(key)) lint_schema(schema[key], where + "/" + key, problems);
  }
  if (schema.contains("oneOf")) {
    for (std::size_t i = 0; i < schema["oneOf"].size(); ++i) {
      lint_schema(schema["oneOf"][i], where + "/oneOf/" + std::to_string(i), problems);
    }
  }
}

bool has_type(const Json& value, const std::string& type) {
  if (type == "object") return value.is_object();
  if (type == "array") return value.is_array();
  if (type == "string") return value.is_string();
  if (type == "integer") return value.is_number_integer();
  if (type == "number") return value.is_number();
  if (type == "boolean") return value.is_boolean();
  if (type == "null") return value.is_null();
  return false;
}

void validate(const Json& schema, const Json& value, const Json& root, const std::string& path,
              std::vector<std::string>& errors) {
  auto fail = [&](const std::string& message) { errors.push_back((path.empty() ? "/" : path) + ": " + message); };
  if (schema.is_boolean()) {
    if (!schema.get<bool>()) fail("not allowed");
    return;
  }
  if (schema.contains("$ref")) {
    const std::string ref = schema["$ref"].get<std::string>();
    const std::string prefix = "#/$defs/";
    if (ref.rfind(prefix, 0) != 0 || !root.contains("$defs") || !root["$defs"].contains(ref.substr(prefix.size()))) {
      fail("unresolvable $ref " + ref);
      return;
    }
    validate(root["$defs"][ref.substr(prefix.size())], value, root, path, errors);
  }
  if (schema.contains("type")) {
    const Json& type = schema["type"];
    bool ok = false;
    if (type.is_array()) {
      for (const Json& t : type) ok = ok || has_type(value, t.get<std::string>());
    } else {
      ok = has_type(value, type.get<std::string>());
    }
    if (!ok) {
      fail("expected type " + type.dump() + ", got " + value.dump());
      return;
    }
  }
  if (schema.contains("const") && value != schema["const"]) fail("expected const " + schema["const"].dump());
  if (schema.contains("enum")) {
    bool found = false;
    for (const Json& option : schema["enum"]) found = found || option == value;
    if (!found) fail("not in enum " + schema["enum"].dump());
  }
  if (value.is_number()) {
    const double number = value.get<double>();
    if (schema.contains("minimum") && number < schema["minimum"].get<double>()) fail("below minimum");
    if (schema.contains("maximum") && number > schema["maximum"].get<double>()) fail("above maximum");
    if (schema.contains("exclusiveMinimum") && number <= schema["exclusiveMinimum"].get<double>()) {
      fail("not above exclusiveMinimum");
    }
  }
  if (value.is_string()) {
    const std::string& text = value.get_ref<const std::string&>();
    if (schema.contains("minLength")) {
      std::size_t length = 0;
      for (unsigned char c : text) length += (c & 0xC0) != 0x80;  // code points
      if (length < schema["minLength"].get<std::size_t>()) fail("shorter than minLength");
    }
    if (schema.contains("pattern") && !std::regex_search(text, std::regex(schema["pattern"].get<std::string>()))) {
      fail("does not match pattern " + schema["pattern"].dump());
    }
  }
  if (value.is_array()) {
    if (schema.contains("minItems") && value.size() < schema["minItems"].get<std::size_t>()) fail("too few items");
    if (schema.contains("maxItems") && value.size() > schema["maxItems"].get<std::size_t>()) fail("too many items");
    if (schema.contains("items")) {
      for (std::size_t i = 0; i < value.size(); ++i) {
        validate(schema["items"], value[i], root, path + "/" + std::to_string(i), errors);
      }
    }
  }
  if (value.is_object()) {
    if (schema.contains("required")) {
      for (const Json& name : schema["required"]) {
        if (!value.contains(name.get<std::string>())) fail("missing required " + name.dump());
      }
    }
    for (const auto& [key, member] : value.items()) {
      const std::string child = path + "/" + key;
      if (schema.contains("propertyNames")) validate(schema["propertyNames"], Json(key), root, child + " (name)", errors);
      if (schema.contains("properties") && schema["properties"].contains(key)) {
        validate(schema["properties"][key], member, root, child, errors);
      } else if (schema.contains("additionalProperties")) {
        validate(schema["additionalProperties"], member, root, child, errors);
      }
    }
  }
  if (schema.contains("oneOf")) {
    int matches = 0;
    for (const Json& branch : schema["oneOf"]) {
      std::vector<std::string> scratch;
      validate(branch, value, root, path, scratch);
      matches += scratch.empty();
    }
    if (matches != 1) fail("oneOf matched " + std::to_string(matches) + " branches");
  }
}

std::vector<std::string> check(const Json& schema, const Json& value) {
  std::vector<std::string> errors;
  validate(schema, value, schema, "", errors);
  return errors;
}

struct Schema {
  Json json;
  int version = 0;
  Json example;  // first validated example, for the negative cases
  int validated = 0;
};

// Validates `json` and any object nested in it (project.json embeds hud and input documents) whose `format`
// names an `arcane.*` schema, when it has the schema's canonical version. Legacy files have no `format`.
void validate_documents(const Json& json, const std::string& file, std::map<std::string, Schema>& schemas) {
  if (json.is_array()) {
    for (const Json& item : json) validate_documents(item, file, schemas);
    return;
  }
  if (!json.is_object()) return;
  if (json.contains("format") && json["format"].is_string() && json["format"].get<std::string>().rfind("arcane.", 0) == 0) {
    const std::string format = json["format"].get<std::string>();
    auto it = schemas.find(format);
    if (it == schemas.end()) {
      std::fprintf(stderr, "%s: no schema for format %s\n", file.c_str(), format.c_str());
      std::exit(1);
    }
    if (json.contains("version") && json["version"] == it->second.version) {  // other versions: not in scope
      const auto errors = check(it->second.json, json);
      for (const auto& e : errors) std::fprintf(stderr, "%s: %s\n", file.c_str(), e.c_str());
      ARCANE_ASSERT_TRUE(errors.empty());
      if (it->second.validated++ == 0) it->second.example = json;
    }
  }
  for (const auto& [key, member] : json.items()) validate_documents(member, file, schemas);
}

void check_readers(const fs::path& dir) {
  {
    auto atlas = arcane::load_atlas_from_json(read(dir / "atlas.example.json"));
    ARCANE_ASSERT_TRUE(atlas.has_value());
    ARCANE_ASSERT_TRUE(atlas->regions.size() == 2);
  }
  {
    auto save = arcane::load_save_game_from_json(read(dir / "save.example.json"));
    ARCANE_ASSERT_TRUE(save.has_value());
    ARCANE_ASSERT_TRUE(save->data_version == 2);
  }
  {
    auto trace = arcane::load_trace_json(read(dir / "replay_trace.example.json"));
    ARCANE_ASSERT_TRUE(trace.has_value());
    ARCANE_ASSERT_TRUE(trace->size() == 2);
  }
  {
    arcane::ComponentRegistry registry;
    arcane::register_builtin_components(registry);
    auto prefab = arcane::Prefab::from_json(read(dir / "prefab.example.json"), registry);
    if (!prefab) std::fprintf(stderr, "prefab example rejected: %s\n", prefab.error().c_str());
    ARCANE_ASSERT_TRUE(prefab.has_value());
  }
}

}  // namespace

int main() {
  const fs::path schema_dir = ARCANE_SCHEMA_DIR;
  const fs::path examples_dir = fs::path(ARCANE_GOLDEN_DIR) / "schema_examples";

  // 1. Every schema: canonical, only supported keywords, a format/version const.
  std::map<std::string, Schema> schemas;  // keyed by format name, e.g. "arcane.atlas"
  std::vector<std::string> problems;
  for (const auto& entry : fs::directory_iterator(schema_dir)) {
    const std::string file = entry.path().filename().string();
    if (file.size() < 12 || file.rfind("arcane.", 0) != 0 || file.substr(file.size() - 12) != ".schema.json") continue;
    const std::string text = read(entry.path());
    auto canonical = arcane::canonicalize_json(text);
    if (!canonical || *canonical != text) problems.push_back(file + ": not canonical JSON");
    Schema schema;
    schema.json = parse(text, file);
    lint_schema(schema.json, file, problems);
    const Json& props = schema.json["properties"];
    const std::string format = props["format"]["const"].get<std::string>();
    if (format + ".schema.json" != file) problems.push_back(file + ": format const " + format + " does not match file name");
    schema.version = props["version"]["const"].get<int>();
    schemas[format] = std::move(schema);
  }
  for (const auto& p : problems) std::fprintf(stderr, "%s\n", p.c_str());
  ARCANE_ASSERT_TRUE(problems.empty());
  ARCANE_ASSERT_TRUE(schemas.size() == 11);

  // 2. Real files: every canonical-version file with an `arcane.*` format must validate.
  check_readers(examples_dir);
  for (const fs::path& root : {fs::path(ARCANE_SAMPLES_DIR), fs::path(ARCANE_GOLDEN_DIR)}) {
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      const std::string text = read(entry.path());
      const Json json = parse(text, entry.path().string());
      if (entry.path().parent_path() == examples_dir) {
        auto canonical = arcane::canonicalize_json(text);
        if (!canonical || *canonical != text) {
          std::fprintf(stderr, "%s: example is not canonical\n", entry.path().string().c_str());
          return 1;
        }
      }
      validate_documents(json, entry.path().string(), schemas);
    }
  }
  for (const auto& [format, schema] : schemas) {
    if (schema.validated == 0) std::fprintf(stderr, "no validated example for %s\n", format.c_str());
    ARCANE_ASSERT_TRUE(schema.validated > 0);
  }

  // 3. Negative cases: a wrong format const and (where the schema forbids them) unknown top-level fields fail.
  for (const auto& [format, schema] : schemas) {
    Json wrong = schema.example;
    wrong["format"] = format + "_x";
    ARCANE_ASSERT_TRUE(!check(schema.json, wrong).empty());

    Json missing = schema.example;
    missing.erase("version");
    ARCANE_ASSERT_TRUE(!check(schema.json, missing).empty());

    if (schema.json.contains("additionalProperties") && schema.json["additionalProperties"] == false) {
      Json extra = schema.example;
      extra["unknown_field_for_test"] = 1;
      ARCANE_ASSERT_TRUE(!check(schema.json, extra).empty());
    }
  }
  return 0;
}
