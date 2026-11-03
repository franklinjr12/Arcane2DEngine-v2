// Canonical JSON writer/parser and the v2 project/scene readers' failure
// paths (no modules needed).
#include <json.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
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

// ---- seeded fuzz: deterministic, no dependence on the platform's <random> distributions ----

struct Rng {
  std::mt19937_64 engine;
  explicit Rng(uint64_t seed) : engine(seed) {}
  uint64_t next() { return engine(); }
  uint64_t below(uint64_t n) { return next() % n; }
  bool chance(unsigned percent) { return below(100) < percent; }
};

// A generated value: the expected data (nlohmann, floats widened from binary32) plus the
// text, emitted with random whitespace, escapes and key order.
struct Gen {
  Rng& rng;
  std::string text;
  nlohmann::json value;
};

void append_utf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += char(cp);
  } else if (cp < 0x800) {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  } else {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

// Returns the string's value (UTF-8) and appends a quoted, randomly escaped form to `text`.
std::string gen_string(Rng& rng, std::string& text) {
  std::string value;
  text += '"';
  const int len = int(rng.below(8));
  for (int i = 0; i < len; ++i) {
    uint32_t cp;
    switch (rng.below(5)) {
      case 0: cp = uint32_t(rng.below(0x80)); break;
      case 1: cp = uint32_t(0x80 + rng.below(0x780)); break;
      case 2: {
        cp = uint32_t(0x800 + rng.below(0xF800));
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        break;
      }
      case 3: cp = uint32_t(0x10000 + rng.below(0x100000)); break;
      default: cp = "\"\\/\b\f\n\r\t\x01\x1f \x7f"[rng.below(11)]; break;
    }
    append_utf8(value, cp);
    const bool escape = rng.chance(40);
    char buf[16];
    if (cp == '"' || cp == '\\') {
      text += '\\';
      text += char(cp);
    } else if (escape && cp < 0x10000) {
      std::snprintf(buf, sizeof buf, rng.chance(50) ? "\\u%04x" : "\\u%04X", cp);
      text += buf;
    } else if (escape) {
      const uint32_t v = cp - 0x10000;
      std::snprintf(buf, sizeof buf, "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
      text += buf;
    } else if (cp < 0x20) {
      std::snprintf(buf, sizeof buf, "\\u%04x", cp);  // control characters must be escaped
      text += buf;
    } else {
      append_utf8(text, cp);
    }
  }
  text += '"';
  return value;
}

void ws(Rng& rng, std::string& text) {
  static const char* const kinds[] = {"", "", " ", "\n", "\t", "\r\n", "  \n  "};
  text += kinds[rng.below(7)];
}

float random_float(Rng& rng) {
  static const float edges[] = {0.0f,
                                -0.0f,
                                1.0f,
                                -1.0f,
                                0.1f,
                                1e-5f,
                                9.99999e-6f,
                                1e-7f,
                                999999999999999.0f,
                                1e15f,
                                1e16f,
                                16777216.0f,
                                16777217.0f,
                                std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::min(),
                                std::numeric_limits<float>::denorm_min(),
                                -std::numeric_limits<float>::denorm_min()};
  if (rng.chance(25)) return edges[rng.below(std::size(edges))];
  for (;;) {
    const uint32_t bits = uint32_t(rng.next());
    float f;
    std::memcpy(&f, &bits, sizeof f);
    if (std::isfinite(f)) return f;
  }
}

void gen_value(Rng& rng, int depth, std::string& text, nlohmann::json& value) {
  const int kinds = depth >= 5 ? 6 : 8;  // containers only above the depth limit
  switch (rng.below(kinds)) {
    case 0: text += "null"; value = nullptr; break;
    case 1: {
      const bool b = rng.chance(50);
      text += b ? "true" : "false";
      value = b;
      break;
    }
    case 2: {  // signed integer, including the extremes
      const int64_t extremes[] = {0, -1, 1, INT64_MIN, INT64_MAX, -9007199254740993LL, 9007199254740993LL};
      int64_t v = rng.chance(30) ? extremes[rng.below(7)] : int64_t(rng.next() >> rng.below(64));
      if (rng.chance(50) && v != INT64_MIN) v = -v;
      text += std::to_string(v);
      value = v;
      break;
    }
    case 3: {  // unsigned 64-bit id range
      const uint64_t v = rng.chance(30) ? UINT64_MAX - rng.below(3) : (rng.next() >> rng.below(64)) | (uint64_t(1) << 63);
      text += std::to_string(v);
      value = v;
      break;
    }
    case 4: {  // binary32 value written with 9 significant digits, sometimes as exponent form
      const float f = random_float(rng);
      char buf[64];
      std::snprintf(buf, sizeof buf, rng.chance(50) ? "%.9g" : "%.9e", double(f));
      std::string num = buf;
      if (num.find_first_of(".eE") == std::string::npos) num += rng.chance(50) ? ".0" : "e0";
      text += num;
      value = double(f);
      break;
    }
    case 5: value = gen_string(rng, text); break;
    case 6: {
      text += '[';
      value = nlohmann::json::array();
      const int n = int(rng.below(5));
      for (int i = 0; i < n; ++i) {
        if (i) text += ',';
        ws(rng, text);
        nlohmann::json child;
        gen_value(rng, depth + 1, text, child);
        value.push_back(std::move(child));
        ws(rng, text);
      }
      text += ']';
      break;
    }
    default: {
      // unique keys, emitted in random order (the data model has no order)
      const int n = int(rng.below(5));
      std::vector<std::pair<std::string, std::string>> members;  // key value -> member text
      value = nlohmann::json::object();
      for (int i = 0; i < n; ++i) {
        std::string key_text;
        const std::string key = gen_string(rng, key_text);
        if (value.contains(key)) continue;
        std::string member = key_text;
        ws(rng, member);
        member += ':';
        ws(rng, member);
        nlohmann::json child;
        gen_value(rng, depth + 1, member, child);
        value[key] = std::move(child);
        members.emplace_back(key, std::move(member));
      }
      for (size_t i = members.size(); i > 1; --i) std::swap(members[i - 1], members[rng.below(i)]);
      text += '{';
      for (size_t i = 0; i < members.size(); ++i) {
        if (i) text += ',';
        ws(rng, text);
        text += members[i].second;
        ws(rng, text);
      }
      text += '}';
      break;
    }
  }
}

// Canonical numbers: integers stay exact, everything else is a binary32 float.
bool same_value(const nlohmann::json& expected, const nlohmann::json& actual) {
  if (expected.is_number_float()) {
    return actual.is_number_float() && float(actual.get<double>()) == float(expected.get<double>());
  }
  if (expected.is_number_integer()) {
    // text such as 5 is parsed unsigned by nlohmann; compare numerically, not by storage type
    return actual.is_number_integer() && !actual.is_number_float() && expected == actual;
  }
  if (expected.is_array()) {
    if (!actual.is_array() || actual.size() != expected.size()) return false;
    for (size_t i = 0; i < expected.size(); ++i) {
      if (!same_value(expected[i], actual[i])) return false;
    }
    return true;
  }
  if (expected.is_object()) {
    if (!actual.is_object() || actual.size() != expected.size()) return false;
    for (const auto& [k, v] : expected.items()) {
      if (!actual.contains(k) || !same_value(v, actual[k])) return false;
    }
    return true;
  }
  return expected == actual;
}

// Positions of object keys are sorted bytewise: check the canonical text through the parser's
// ordered view (a plain nlohmann object also sorts, so scan the raw text instead).
bool keys_sorted(const nlohmann::ordered_json& v) {
  if (v.is_object()) {
    std::string previous;
    bool first = true;
    for (const auto& [k, child] : v.items()) {
      if (!first && !(previous < k)) return false;
      first = false;
      previous = k;
      if (!keys_sorted(child)) return false;
    }
  } else if (v.is_array()) {
    for (const auto& child : v) {
      if (!keys_sorted(child)) return false;
    }
  }
  return true;
}

void test_fuzz_round_trip() {
  const auto start = std::chrono::steady_clock::now();
  Rng rng(0xA2C4E6F8DEADBEEFULL);
  const int iterations = 12000;
  for (int i = 0; i < iterations; ++i) {
    // the top level is always a container so every strict prefix of the output is malformed
    std::string text;
    nlohmann::json value;
    text += rng.chance(50) ? "[" : "{";
    const bool is_array = text[0] == '[';
    std::string inner;
    nlohmann::json child;
    {
      std::string body;
      nlohmann::json v;
      gen_value(rng, 1, body, v);
      if (is_array) {
        value = nlohmann::json::array({v});
        text += body;
        text += "]";
      } else {
        text += "\"k\":" + body + "}";
        value = nlohmann::json::object({{"k", v}});
      }
    }
    ws(rng, text);

    auto once = arcane::canonicalize_json(text);
    if (!once) std::fprintf(stderr, "iteration %d rejected: %s\n%s\n", i, once.error().c_str(), text.c_str());
    ARCANE_ASSERT_TRUE(once.has_value());
    // idempotent, and the output is LF-only with one trailing newline
    auto twice = arcane::canonicalize_json(*once);
    ARCANE_ASSERT_TRUE(twice.has_value() && *twice == *once);
    ARCANE_ASSERT_TRUE(once->back() == '\n' && (*once)[once->size() - 2] != '\n');
    ARCANE_ASSERT_TRUE(once->find('\r') == std::string::npos);
    // the data survives (integers exact, floats as binary32)
    const nlohmann::json parsed = nlohmann::json::parse(*once);
    if (!same_value(value, parsed)) std::fprintf(stderr, "iteration %d changed value\n%s\n--\n%s\n", i, text.c_str(), once->c_str());
    ARCANE_ASSERT_TRUE(same_value(value, parsed));
    // key order stable: sorted in the output, and the ordered parse re-emits the same bytes
    ARCANE_ASSERT_TRUE(keys_sorted(nlohmann::ordered_json::parse(*once)));
    // equal data written with different key order / whitespace gives identical bytes
    std::string reshuffled = parsed.dump(rng.chance(50) ? -1 : 2);
    auto again = arcane::canonicalize_json(reshuffled);
    ARCANE_ASSERT_TRUE(again.has_value());
    ARCANE_ASSERT_TRUE(*again == *once);

    // truncation: any strict prefix that cuts into the document is rejected
    if (i % 4 == 0) {
      const size_t cut = rng.below(once->size() - 1);
      ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(once->substr(0, cut)).has_value());
    }
    // random byte damage: must return (value or error), never crash; if accepted, still idempotent
    if (i % 4 == 1) {
      std::string damaged = *once;
      for (int n = 1 + int(rng.below(3)); n > 0; --n) damaged[rng.below(damaged.size())] = char(rng.below(256));
      auto result = arcane::canonicalize_json(damaged);
      if (result) {
        auto result2 = arcane::canonicalize_json(*result);
        ARCANE_ASSERT_TRUE(result2.has_value() && *result2 == *result);
      }
    }
  }
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("canonical fuzz: %d iterations in %.2f s\n", iterations, elapsed);
  // No wall-clock budget under sanitizers (ASan builds run several times slower).
#if defined(__SANITIZE_ADDRESS__)
  constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
  constexpr bool kSanitized = true;
#else
  constexpr bool kSanitized = false;
#endif
#else
  constexpr bool kSanitized = false;
#endif
  if (!kSanitized) ARCANE_ASSERT_TRUE(elapsed < 5.0);
}

void test_bad_utf8_and_malformed_rejected() {
  const char* const bad_strings[] = {"\xFF",         "\x80",         "\xC0\x80",     "\xC1\xBF",     "\xE2\x82",
                                     "\xE2\x28\xA1", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF8\x88\x80\x80\x80", "a\xC3"};
  for (const char* bad : bad_strings) {
    ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(std::string("[\"") + bad + "\"]").has_value());
    ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(std::string("{\"") + bad + "\":1}").has_value());
  }
  const char* const malformed[] = {"",         " ",        "[",       "{",         "[1,]",      "{\"a\":1,}", "{\"a\"}",
                                   "[1 2]",    "{\"a\":1}{", "[\"\\ud800\"]", "[\"\\u12\"]", "[\"\\x\"]", "[01]",
                                   "[1.]",     "[.5]",     "[--1]",   "[+1]",      "[tru]",     "[nul]",      "{'a':1}",
                                   "[1e]",     "[Infinity]", "[-NaN]", "{\"a\":1,\"a\":2}", "[\"a\nb\"]"};
  for (const char* text : malformed) {
    ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(text).has_value());
  }
  // a leading UTF-8 BOM is skipped by the parser and never reaches the canonical output
  ARCANE_ASSERT_TRUE(canon("\xEF\xBB\xBF[]") == "[]\n");
  // deep nesting is rejected with an error instead of overflowing the stack
  ARCANE_ASSERT_TRUE(arcane::canonicalize_json(std::string(100, '[') + std::string(100, ']')).has_value());
  for (size_t depth : {size_t(10000), size_t(200000)}) {
    ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(std::string(depth, '[') + std::string(depth, ']')).has_value());
    ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(std::string(depth, '[')).has_value());
    std::string objects;
    for (size_t i = 0; i < depth; ++i) objects += "{\"a\":";
    ARCANE_ASSERT_TRUE(!arcane::canonicalize_json(objects).has_value());
  }
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
  test_fuzz_round_trip();
  test_bad_utf8_and_malformed_rejected();
  return 0;
}
