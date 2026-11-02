// Canonical JSON writer/parser and the v2 project/scene readers' failure
// paths (no modules needed).
#include <json.hpp>

#include <cmath>
#include <filesystem>
#include <limits>

#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/migration.hpp"
#include "arcane/project.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

std::string canon(const std::string& text) {
  auto out = arcane::canonicalize_json(text);
  ARCANE_ASSERT_TRUE(out.has_value());
  return *out;
}

void test_format_rules() {
  // sorted keys, two spaces, one element per line, trailing LF, empty containers inline
  ARCANE_ASSERT_TRUE(canon(R"({"b":[1,2],"a":{},"c":[]})") ==
                     "{\n  \"a\": {},\n  \"b\": [\n    1,\n    2\n  ],\n  \"c\": []\n}\n");
  ARCANE_ASSERT_TRUE(canon("[ ]") == "[]\n");
}

void test_float_policy() {
  ARCANE_ASSERT_TRUE(canon("[1.0]") == "[\n  1.0\n]\n");
  ARCANE_ASSERT_TRUE(canon("[1]") == "[\n  1\n]\n");  // integers stay integers
  ARCANE_ASSERT_TRUE(canon("[-0.0]") == "[\n  0.0\n]\n");
  ARCANE_ASSERT_TRUE(canon("[0.1]") == "[\n  0.1\n]\n");
  ARCANE_ASSERT_TRUE(canon("[0.8500000238418579]") == "[\n  0.85\n]\n");  // binary32 shortest
  ARCANE_ASSERT_TRUE(canon("[1e-5]") == "[\n  0.00001\n]\n");
  ARCANE_ASSERT_TRUE(canon("[1e-7]") == "[\n  1e-7\n]\n");
  ARCANE_ASSERT_TRUE(canon("[1.5E+20]") == "[\n  1.5e20\n]\n");
  ARCANE_ASSERT_TRUE(canon("[123456.789]") == "[\n  123456.79\n]\n");
  ARCANE_ASSERT_TRUE(canon("[100.0]") == "[\n  100.0\n]\n");
  ARCANE_ASSERT_TRUE(canon("[18446744073709551615]") == "[\n  18446744073709551615\n]\n");  // uint64 ids
  ARCANE_ASSERT_TRUE(!arcane::canonicalize_json("[1e999]").has_value());  // out of range
  ARCANE_ASSERT_TRUE(!arcane::canonicalize_json("[NaN]").has_value());

  // every value round-trips through text and re-formatting is a fixed point
  const float samples[] = {0.1f, 1.0f / 3.0f, 3.14159265f, 1e-6f, 123456789.0f, 1e10f, 2.5e-30f, 16777216.0f,
                           std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()};
  for (float f : samples) {
    for (float v : {f, -f}) {
      nlohmann::json j = nlohmann::json::array({v});
      auto once = arcane::canonicalize_json(j.dump(17));
      ARCANE_ASSERT_TRUE(once.has_value());
      ARCANE_ASSERT_TRUE(canon(*once) == *once);
      auto parsed = nlohmann::json::parse(*once);
      ARCANE_ASSERT_TRUE(parsed[0].get<float>() == v);
    }
  }
}

void test_rejects_bad_input() {
  ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(R"({"a":1,"a":2})").has_value());
  ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(R"({"a":1} trailing)").has_value());
  ARCANE_ASSERT_TRUE(!arcane::canonicalize_json("{").has_value());
  ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(std::string("[\"\xff\"]")).has_value());  // invalid UTF-8
}

void test_future_and_unknown_versions() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  const std::string base = R"("name":"p","main_scene":"scenes/main.scene.json")";
  auto future = arcane::load_project_from_json(R"({"format":"arcane.project","version":3,)" + base + "}");
  ARCANE_ASSERT_TRUE(!future && future.error().find("newer") != std::string::npos);
  ARCANE_ASSERT_TRUE(!arcane::load_project_from_json(R"({"format":"arcane.project","version":1,)" + base + "}"));
  ARCANE_ASSERT_TRUE(!arcane::load_project_from_json(R"({"format":"arcane.scene","version":2,)" + base + "}"));
  ARCANE_ASSERT_TRUE(!arcane::load_project_from_json(R"({"format_version":2,)" + base + "}"));
  ARCANE_ASSERT_TRUE(!arcane::Scene::load_from_json(R"({"format":"arcane.scene","version":3,"nodes":[]})", registry));
  ARCANE_ASSERT_TRUE(!arcane::Scene::load_from_json(R"({"format":"arcane.scene","version":"2","nodes":[]})", registry));
  ARCANE_ASSERT_TRUE(arcane::Scene::load_from_json(R"({"format":"arcane.scene","version":2,"nodes":[]})", registry));
}

void test_v2_is_strict() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  auto load = [&](const std::string& body) {
    return arcane::Scene::load_from_json(R"({"format":"arcane.scene","version":2,"nodes":[)" + body + "]}", registry);
  };
  ARCANE_ASSERT_TRUE(load(R"({"id":1,"name":"a"})"));
  ARCANE_ASSERT_TRUE(!load(R"({"id":1,"name":"a","extra":1})"));            // unknown node field
  ARCANE_ASSERT_TRUE(!load(R"({"id":1,"name":"a"},{"id":1,"name":"b"})"));  // duplicate id
  ARCANE_ASSERT_TRUE(!load(R"({"id":1,"name":"a","position":[1]})"));
  ARCANE_ASSERT_TRUE(!load(R"({"id":1,"name":"a","components":[{"id":1,"type":"Camera"}]})"));  // id shared with node
  ARCANE_ASSERT_TRUE(!load(R"({"id":1,"name":"a","components":[{"id":2,"type":"Camera","properties":{"nope":1}}]})"));
  ARCANE_ASSERT_TRUE(!load(R"({"id":1,"name":"a","components":[{"id":2,"type":"Camera","properties":{"zoom":"x"}}]})"));
  ARCANE_ASSERT_TRUE(!load(R"({"id":1,"name":"a","components":[{"id":2,"type":"Camera","properties":{"primary":1}}]})"));
  ARCANE_ASSERT_TRUE(load(R"({"id":1,"name":"a","components":[{"id":2,"type":"Camera","properties":{"zoom":2}}]})"));
  ARCANE_ASSERT_TRUE(!load(R"({"id":2,"name":"a","parent_id":9})"));         // missing parent
  ARCANE_ASSERT_TRUE(!load(R"({"id":2,"name":"a","parent_id":3},{"id":3,"name":"b","parent_id":2})"));  // cycle

  // legacy files keep the old leniency for unknown properties, but strict loading refuses them
  const std::string legacy =
      R"({"format_version":1,"nodes":[{"id":1,"name":"a","components":[{"id":2,"type":"Camera","properties":{"nope":1}}]}]})";
  ARCANE_ASSERT_TRUE(arcane::Scene::load_from_json(legacy, registry));
  ARCANE_ASSERT_TRUE(!arcane::Scene::load_from_json_strict(legacy, registry));
  ARCANE_ASSERT_TRUE(!arcane::migrate_scene_json(legacy, registry));
}

void test_project_paths_and_unknowns() {
  auto load = [](const std::string& fields) {
    return arcane::load_project_from_json(R"({"format":"arcane.project","version":2,)" + fields + "}");
  };
  ARCANE_ASSERT_TRUE(load(R"("name":"p","main_scene":"scenes/a.scene.json","module":"m.so")"));
  ARCANE_ASSERT_TRUE(!load(R"("name":"p","main_scene":"../a.scene.json")"));
  ARCANE_ASSERT_TRUE(!load(R"("name":"p","main_scene":"/abs/a.scene.json")"));
  ARCANE_ASSERT_TRUE(!load(R"("name":"p","main_scene":"scenes\\a.scene.json")"));
  ARCANE_ASSERT_TRUE(!load(R"("name":"p","main_scene":"C:/a.scene.json")"));
  ARCANE_ASSERT_TRUE(!load(R"("name":"p","main_scene":"a.scene.json","surprise":true)"));

  // nested documents the loaders cannot fully preserve block migration
  const std::string lossy =
      R"({"format_version":1,"name":"p","main_scene":"s.scene.json","hud":{"format_version":1,"elements":[{"type":"text","text":"x","mystery":5}]}})";
  ARCANE_ASSERT_TRUE(arcane::load_project_from_json(lossy));
  ARCANE_ASSERT_TRUE(!arcane::migrate_project_json(lossy));
}

void test_save_round_trip_and_nonfinite() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  arcane::Scene scene;
  arcane::Node* node = scene.create_node("n");
  node->local_transform().position = {0.1f, -2.5f};
  scene.add_component(node, "Camera", registry);
  auto text = scene.save_canonical_json(registry);
  ARCANE_ASSERT_TRUE(text.has_value());
  auto loaded = arcane::Scene::load_from_json(*text, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->save_canonical_json(registry) == text);
  ARCANE_ASSERT_TRUE(canon(*text) == *text);

  node->local_transform().rotation_radians = std::nanf("");
  ARCANE_ASSERT_TRUE(!scene.save_canonical_json(registry).has_value());
  ARCANE_ASSERT_TRUE(scene.save_to_json(registry).empty());
}

std::string slurp_file(const std::filesystem::path& path) {
  auto text = arcane::read_text_file(path);
  ARCANE_ASSERT_TRUE(text.has_value());
  return *text;
}

void test_transactional_write_rolls_back() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "arcane_test_transactional";
  const fs::path a = dir / "a.json", b = dir / "b.json", c = dir / "c.json";
  auto reset = [&] {
    fs::remove_all(dir);
    fs::create_directories(dir);
    ARCANE_ASSERT_TRUE(arcane::write_text_file_atomic(a, "old a").has_value());
    ARCANE_ASSERT_TRUE(arcane::write_text_file_atomic(b, std::string("old b\0bin", 9)).has_value());
  };
  auto no_temp = [&] {
    for (const auto& entry : fs::directory_iterator(dir)) {
      ARCANE_ASSERT_TRUE(entry.path().extension() != ".arcane-tmp");
    }
  };
  const std::vector<arcane::FileWrite> files = {{a, "new a"}, {b, "new b"}, {c, "new c"}};

  reset();
  ARCANE_ASSERT_TRUE(arcane::write_text_files_transactional(files).has_value());
  ARCANE_ASSERT_TRUE(slurp_file(a) == "new a" && slurp_file(b) == "new b" && slurp_file(c) == "new c");
  no_temp();

  for (int fail = 0; fail < 3; ++fail) {
    reset();
    auto result = arcane::write_text_files_transactional(files, {.fail_commit_index = fail});
    ARCANE_ASSERT_TRUE(!result.has_value());
    ARCANE_ASSERT_TRUE(result.error().starts_with("rolled back: simulated failure committing"));
    ARCANE_ASSERT_TRUE(slurp_file(a) == "old a" && slurp_file(b) == std::string("old b\0bin", 9));
    ARCANE_ASSERT_TRUE(!fs::exists(c));
    no_temp();
  }
  fs::remove_all(dir);
}

}  // namespace

int main() {
  test_format_rules();
  test_float_policy();
  test_rejects_bad_input();
  test_future_and_unknown_versions();
  test_v2_is_strict();
  test_project_paths_and_unknowns();
  test_save_round_trip_and_nonfinite();
  test_transactional_write_rolls_back();
  return 0;
}
