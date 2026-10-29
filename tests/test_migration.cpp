// Migrates both legacy sample projects (with their real game modules) in a
// temp copy: semantic equality, byte-identical repeat runs, golden v2 files,
// and the failure paths (unknown data, future versions, rollback).
// Set ARCANE_UPDATE_GOLDEN=1 to rewrite tests/golden from the current output.
#include <cstdlib>
#include <filesystem>

#include "arcane/canonical_json.hpp"
#include "arcane/migration.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "test_assert.hpp"

namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& path) {
  auto text = arcane::read_text_file(path);
  ARCANE_ASSERT_TRUE(text.has_value());
  return *text;
}

void write(const fs::path& path, const std::string& text) {
  ARCANE_ASSERT_TRUE(arcane::write_text_file_atomic(path, text).has_value());
}

fs::path copy_project(const std::string& source, const std::string& tag) {
  const fs::path dest = fs::temp_directory_path() / ("arcane_migration_" + tag);
  std::error_code ec;
  fs::remove_all(dest, ec);
  fs::copy(source, dest, fs::copy_options::recursive, ec);
  ARCANE_ASSERT_TRUE(!ec);
  return dest;
}

bool has_staged_files(const fs::path& dir) {
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.path().extension() == ".arcane-tmp") return true;
  }
  return false;
}

void replace_first(std::string& text, const std::string& from, const std::string& to) {
  const auto pos = text.find(from);
  ARCANE_ASSERT_TRUE(pos != std::string::npos);
  text.replace(pos, from.size(), to);
}

void check_sample(const std::string& source, const std::string& module, const std::string& name) {
  const fs::path dir = copy_project(source, name);

  // Legacy project loads with its module; remember its canonical scene text.
  auto legacy = arcane::editor::ProjectRuntime::open(dir, module);
  ARCANE_ASSERT_TRUE(legacy.has_value());
  const std::string legacy_scene = legacy->session().scene().save_canonical_json(legacy->registry()).value();
  const std::string legacy_project = arcane::save_project_canonical_json(legacy->session().project()).value();
  const std::string legacy_scene_file = read(dir / "scenes/main.scene.json");
  ARCANE_ASSERT_TRUE(legacy_scene_file.find("format_version") != std::string::npos);

  // Dry run reports changes and writes nothing.
  auto dry = arcane::migrate_project(dir, legacy->registry(), {.dry_run = true});
  ARCANE_ASSERT_TRUE(dry.has_value() && !dry->applied);
  ARCANE_ASSERT_TRUE(dry->files.size() >= 3 && dry->files[0].path == "project.json" && dry->files[0].changed);
  for (const auto& file : dry->files) ARCANE_ASSERT_TRUE(file.changed);  // legacy sample: every file migrates
  std::size_t asset_files = 0;
  for (const auto& file : dry->files) {
    if (file.path.starts_with("assets/")) ++asset_files;
  }
  ARCANE_ASSERT_TRUE(asset_files >= 1);
  ARCANE_ASSERT_TRUE(read(dir / "scenes/main.scene.json") == legacy_scene_file);

  auto report = arcane::migrate_project(dir, legacy->registry());
  ARCANE_ASSERT_TRUE(report.has_value() && report->applied);
  ARCANE_ASSERT_TRUE(!has_staged_files(dir));
  const std::string project_v2 = read(dir / "project.json");
  const std::string scene_v2 = read(dir / "scenes/main.scene.json");
  // Same meaning: migrated files equal what the legacy load re-serialized to.
  ARCANE_ASSERT_TRUE(project_v2 == legacy_project);
  ARCANE_ASSERT_TRUE(project_v2.find("format_version") == std::string::npos);  // nested HUD/input are v2 too
  ARCANE_ASSERT_TRUE(scene_v2 == legacy_scene);

  // Reload the migrated project: IDs and module properties survive.
  auto migrated = arcane::editor::ProjectRuntime::open(dir, module);
  ARCANE_ASSERT_TRUE(migrated.has_value());
  const auto& before = legacy->session().scene();
  const auto& after = migrated->session().scene();
  ARCANE_ASSERT_TRUE(before.next_id() == after.next_id());
  for (arcane::NodeId id = 1; id < before.next_id(); ++id) {
    ARCANE_ASSERT_TRUE((before.find_node(id) == nullptr) == (after.find_node(id) == nullptr));
    ARCANE_ASSERT_TRUE((before.find_component(id) == nullptr) == (after.find_component(id) == nullptr));
    if (const auto* component = after.find_component(id)) {
      ARCANE_ASSERT_TRUE(component->type_name() == before.find_component(id)->type_name());
    }
  }
  ARCANE_ASSERT_TRUE(migrated->session().scene().save_canonical_json(migrated->registry()).value() == scene_v2);

  // Repeating changes nothing, byte for byte; re-formatting is a fixed point.
  auto again = arcane::migrate_project(dir, migrated->registry());
  ARCANE_ASSERT_TRUE(again.has_value() && !again->applied);
  for (const auto& file : again->files) ARCANE_ASSERT_TRUE(!file.changed);
  ARCANE_ASSERT_TRUE(read(dir / "project.json") == project_v2 && read(dir / "scenes/main.scene.json") == scene_v2);
  ARCANE_ASSERT_TRUE(arcane::canonicalize_json(scene_v2).value() == scene_v2);
  ARCANE_ASSERT_TRUE(arcane::canonicalize_json(project_v2).value() == project_v2);

  // Golden files pin the exact bytes across toolchains and platforms.
  const fs::path golden = fs::path(ARCANE_GOLDEN_DIR) / name;
  if (std::getenv("ARCANE_UPDATE_GOLDEN") != nullptr) {
    fs::create_directories(golden / "scenes");
    write(golden / "project.json", project_v2);
    write(golden / "scenes/main.scene.json", scene_v2);
    for (const auto& file : report->files) {
      if (!file.path.starts_with("assets/")) continue;
      fs::create_directories((golden / file.path).parent_path());
      write(golden / file.path, file.new_text);
    }
  }
  for (const auto& file : report->files) {
    if (!file.path.starts_with("assets/")) continue;
    ARCANE_ASSERT_TRUE(read(golden / file.path) == file.new_text);
    ARCANE_ASSERT_TRUE(read(dir / file.path) == file.new_text);
    ARCANE_ASSERT_TRUE(arcane::canonicalize_json(file.new_text).value() == file.new_text);
    ARCANE_ASSERT_TRUE(file.new_text.find("\"format_version\"") == std::string::npos);
  }
  ARCANE_ASSERT_TRUE(read(golden / "project.json") == project_v2);
  ARCANE_ASSERT_TRUE(read(golden / "scenes/main.scene.json") == scene_v2);

  // --- failures leave a fresh legacy copy untouched ---
  const fs::path bad = copy_project(source, name + "_bad");
  const std::string bad_project = read(bad / "project.json");
  const std::string bad_scene = read(bad / "scenes/main.scene.json");
  auto untouched = [&] {
    ARCANE_ASSERT_TRUE(read(bad / "project.json") == bad_project);
    ARCANE_ASSERT_TRUE(read(bad / "scenes/main.scene.json") == bad_scene);
    ARCANE_ASSERT_TRUE(!has_staged_files(bad));
  };

  // unknown component property in the scene (project.json is valid and would migrate first)
  std::string text = bad_scene;
  replace_first(text, "\"properties\": {", "\"properties\": {\n          \"surprise\": 1,");
  write(bad / "scenes/main.scene.json", text);
  auto unknown = arcane::migrate_project(bad, legacy->registry());
  ARCANE_ASSERT_TRUE(!unknown.has_value() && unknown.error().find("surprise") != std::string::npos);
  ARCANE_ASSERT_TRUE(read(bad / "project.json") == bad_project);
  ARCANE_ASSERT_TRUE(!has_staged_files(bad));

  // unknown node field
  text = bad_scene;
  replace_first(text, "\"name\":", "\"tag\": \"x\",\n      \"name\":");
  write(bad / "scenes/main.scene.json", text);
  ARCANE_ASSERT_TRUE(!arcane::migrate_project(bad, legacy->registry()).has_value());
  ARCANE_ASSERT_TRUE(read(bad / "project.json") == bad_project);

  // future scene version
  text = bad_scene;
  replace_first(text, "\"format_version\": 1", "\"format_version\": 3");
  write(bad / "scenes/main.scene.json", text);
  auto future = arcane::migrate_project(bad, legacy->registry());
  ARCANE_ASSERT_TRUE(!future.has_value() && future.error().find("newer") != std::string::npos);
  ARCANE_ASSERT_TRUE(read(bad / "project.json") == bad_project);

  // malformed scene
  write(bad / "scenes/main.scene.json", bad_scene.substr(0, bad_scene.size() / 2));
  ARCANE_ASSERT_TRUE(!arcane::migrate_project(bad, legacy->registry()).has_value());
  ARCANE_ASSERT_TRUE(read(bad / "project.json") == bad_project);

  // failure while committing the second file: first file is rolled back
  write(bad / "scenes/main.scene.json", bad_scene);
  auto rolled = arcane::migrate_project(bad, legacy->registry(), {.fail_commit_index = 1});
  ARCANE_ASSERT_TRUE(!rolled.has_value() && rolled.error().find("rolled back") != std::string::npos);
  untouched();

  // bad registry (module types missing): refuse rather than drop components
  if (name == "platformer_slice") {
    arcane::ComponentRegistry builtin_only;
    ARCANE_ASSERT_TRUE(!arcane::migrate_project(bad, builtin_only).has_value());
    untouched();
  }

  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::remove_all(bad, ec);
}

}  // namespace

// The shipped schemas are canonical files themselves and pin the same header
// constants the readers enforce.
void check_schemas() {
  for (const char* name : {"project", "scene", "tileset", "clip", "hud", "input"}) {
    const std::string text = read(fs::path(ARCANE_SCHEMA_DIR) / (std::string("arcane.") + name + ".schema.json"));
    ARCANE_ASSERT_TRUE(arcane::canonicalize_json(text).value() == text);
    ARCANE_ASSERT_TRUE(text.find(std::string("\"const\": \"arcane.") + name + "\"") != std::string::npos);
    ARCANE_ASSERT_TRUE(text.find("\"const\": 2") != std::string::npos);
  }
}

int main() {
  check_schemas();
  check_sample(ARCANE_SAMPLE_PROJECT_DIR, ARCANE_SAMPLE_GAME_PATH, "sample_project");
  check_sample(ARCANE_SLICE_PROJECT_DIR, ARCANE_SLICE_MODULE_PATH, "platformer_slice");
  return 0;
}
