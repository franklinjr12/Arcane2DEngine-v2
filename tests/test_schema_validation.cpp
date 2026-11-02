// Validates every engine JSON resource against schemas/arcane.*.schema.json:
// the canonical v2 files under tests/golden, and samples/sample_project (still
// legacy v1 on disk) through an in-memory dry-run migration. Broken fixtures
// built in memory from golden text must fail with the file name and the
// RFC 6901 pointer of the offending value. Also unit-checks the validator.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>

#include <json.hpp>

#include "arcane/migration.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "schema_validator.hpp"
#include "test_assert.hpp"

namespace fs = std::filesystem;
using arcane::test::SchemaError;
using nlohmann::json;

namespace {

std::string read(const fs::path& path) {
  std::string text;
  ARCANE_ASSERT_TRUE(arcane::test::schema_detail::read_text(path, text));
  return text;
}

std::vector<fs::path> json_files(const fs::path& dir) {
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::size_t report(const std::vector<SchemaError>& errors) {
  for (const auto& e : errors) std::fprintf(stderr, "schema error: %s\n", e.str().c_str());
  return errors.size();
}

void check_golden() {
  const fs::path root = ARCANE_GOLDEN_DIR;
  const auto files = json_files(root);
  ARCANE_ASSERT_TRUE(files.size() >= 8);
  std::size_t failures = 0;
  for (const auto& file : files) {
    const std::string name = "tests/golden/" + fs::relative(file, root).generic_string();
    failures += report(arcane::test::validate_resource_text(ARCANE_SCHEMA_DIR, read(file), name));
  }
  ARCANE_ASSERT_TRUE(failures == 0);
}

void check_sample() {
  const fs::path source = ARCANE_SAMPLE_PROJECT_DIR;
  // Work on a copy so opening the project can never touch the checkout.
  const fs::path dir = fs::temp_directory_path() / "arcane_schema_sample_project";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::copy(source, dir, fs::copy_options::recursive, ec);
  ARCANE_ASSERT_TRUE(!ec);

  auto rt = arcane::editor::ProjectRuntime::open(dir, ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(rt.has_value());
  auto dry = arcane::migrate_project(dir, rt->registry(), {.dry_run = true});
  ARCANE_ASSERT_TRUE(dry.has_value() && !dry->applied);

  std::size_t failures = 0;
  std::set<std::string> covered;
  for (const auto& file : dry->files) {
    covered.insert(file.path);
    failures += report(arcane::test::validate_resource_text(ARCANE_SCHEMA_DIR, file.new_text, "samples/sample_project/" + file.path));
  }
  // Every JSON file on disk is either in the migration report or already v2.
  std::size_t seen = 0;
  for (const auto& file : json_files(source)) {
    const std::string rel = fs::relative(file, source).generic_string();
    ++seen;
    if (covered.count(rel) != 0) continue;
    const json doc = json::parse(read(file), nullptr, false);
    if (doc.is_object() && doc.contains("format")) {
      failures += report(arcane::test::validate_resource_text(ARCANE_SCHEMA_DIR, read(file), "samples/sample_project/" + rel));
    } else {
      std::fprintf(stderr, "samples/sample_project/%s: not validated (neither migrated nor v2)\n", rel.c_str());
      ++failures;
    }
  }
  ARCANE_ASSERT_TRUE(seen >= 3);
  ARCANE_ASSERT_TRUE(failures == 0);
  fs::remove_all(dir, ec);
}

std::string golden(const char* relative) { return read(fs::path(ARCANE_GOLDEN_DIR) / relative); }

// Validates `doc` and requires an error whose formatted text names `file` and `pointer`.
void expect_error(const json& doc, const std::string& file, const std::string& pointer, const std::string& message_part = "") {
  const auto errors = arcane::test::validate_resource_text(ARCANE_SCHEMA_DIR, doc.dump(2), file);
  bool found = false;
  for (const auto& e : errors) {
    const std::string text = e.str();
    if (text.find(file) != std::string::npos && e.pointer == pointer && text.find(message_part) != std::string::npos) found = true;
  }
  if (!found) {
    std::fprintf(stderr, "expected error at '%s' in %s; got %zu errors:\n", pointer.c_str(), file.c_str(), errors.size());
    report(errors);
  }
  ARCANE_ASSERT_TRUE(found);
}

void check_broken_fixtures() {
  {
    json doc = json::parse(golden("sample_project/assets/tiles.json"));
    doc["columns"] = 0;
    expect_error(doc, "broken/tiles.json", "/columns");
  }
  {
    json doc = json::parse(golden("sample_project/scenes/main.scene.json"));
    doc["nodes"][0]["position"] = json::array({1.0, 2.0, 3.0});
    expect_error(doc, "broken/main.scene.json", "/nodes/0/position");
  }
  {
    json doc = json::parse(golden("sample_project/scenes/main.scene.json"));
    doc["bogus"] = 1;
    expect_error(doc, "broken/bogus.scene.json", "/bogus", "bogus");
  }
  {
    json doc = json::parse(golden("sample_project/project.json"));
    doc["main_scene"] = "/abs/path.json";
    expect_error(doc, "broken/project.json", "/main_scene");
  }
  {
    json doc = json::parse(golden("platformer_slice/project.json"));
    ARCANE_ASSERT_TRUE(!doc["hud"]["elements"].empty());
    doc["hud"]["elements"][0]["anchor"] = "middle_of_nowhere";
    const auto errors = arcane::test::validate_resource_text(ARCANE_SCHEMA_DIR, doc.dump(2), "broken/hud_project.json");
    ARCANE_ASSERT_TRUE(!errors.empty());
    for (const auto& e : errors) {
      ARCANE_ASSERT_TRUE(e.pointer.starts_with("/hud/"));
      ARCANE_ASSERT_TRUE(e.str().find("broken/hud_project.json") != std::string::npos);
    }
    expect_error(doc, "broken/hud_project.json", "/hud/elements/0/anchor");
  }
  {
    json doc = json::parse(golden("sample_project/scenes/main.scene.json"));
    doc["format"] = "arcane.nope";
    const auto errors = arcane::test::validate_resource_text(ARCANE_SCHEMA_DIR, doc.dump(2), "broken/nope.json");
    ARCANE_ASSERT_TRUE(!errors.empty());
    ARCANE_ASSERT_TRUE(errors[0].str().find("broken/nope.json") != std::string::npos);
  }
  {
    const auto errors = arcane::test::validate_resource_text(ARCANE_SCHEMA_DIR, "{\"version\": 2}", "broken/noformat.json");
    ARCANE_ASSERT_TRUE(errors.size() == 1 && errors[0].file == "broken/noformat.json");
  }
}

void check_validator_units() {
  const json integer_schema = json::parse(R"({"type":"integer","minimum":0})");
  ARCANE_ASSERT_TRUE(arcane::test::validate(integer_schema, json(13743565431654770875ULL), "u").empty());
  ARCANE_ASSERT_TRUE(arcane::test::validate(integer_schema, json(3.0), "u").empty());
  ARCANE_ASSERT_TRUE(!arcane::test::validate(integer_schema, json(3.5), "u").empty());
  ARCANE_ASSERT_TRUE(!arcane::test::validate(integer_schema, json(-1), "u").empty());

  const json one_of = json::parse(R"({"oneOf":[{"type":"integer"},{"type":"number"}]})");
  ARCANE_ASSERT_TRUE(!arcane::test::validate(one_of, json(1), "u").empty());   // both match
  ARCANE_ASSERT_TRUE(arcane::test::validate(one_of, json(1.5), "u").empty());  // exactly one
  ARCANE_ASSERT_TRUE(!arcane::test::validate(one_of, json("x"), "u").empty()); // none

  const json ref_schema = json::parse(R"({"$defs":{"pair":{"type":"array","minItems":2,"maxItems":2}},
    "properties":{"a/b":{"$ref":"#/$defs/pair"}}})");
  ARCANE_ASSERT_TRUE(arcane::test::validate(ref_schema, json::parse(R"({"a/b":[1,2]})"), "u").empty());
  const auto bad = arcane::test::validate(ref_schema, json::parse(R"({"a/b":[1]})"), "u");
  ARCANE_ASSERT_TRUE(bad.size() == 1 && bad[0].pointer == "/a~1b");

  // Unsupported schema keywords must fail loudly, not be ignored.
  ARCANE_ASSERT_TRUE(!arcane::test::validate(json::parse(R"({"allOf":[]})"), json(1), "u").empty());
}

}  // namespace

int main() {
  check_validator_units();
  check_golden();
  check_sample();
  check_broken_fixtures();
  return 0;
}
