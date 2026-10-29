#pragma once

// Engine-internal helpers shared by the project/scene (de)serializers:
// strict parsing, the canonical writer and small typed accessors that fail
// with a readable message instead of nlohmann's exceptions. Not installed.
// Format rules: docs/formats/canonical-json.md.
#include <json.hpp>

#include <cstdint>
#include <expected>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

#include "arcane/math2d.hpp"
#include "arcane/renderer.hpp"

namespace arcane::canonical {

// Thrown by the accessors below; the (de)serializers catch it at their
// entry point and turn it into std::unexpected.
class SchemaError : public std::runtime_error {
 public:
  explicit SchemaError(const std::string& message) : std::runtime_error(message) {}
};

// Parses `text`, rejecting duplicate object keys, trailing garbage and
// non-finite numbers (the plain nlohmann parser keeps the last duplicate).
std::expected<nlohmann::json, std::string> parse_strict(const std::string& text);

// Canonical form: UTF-8, LF, two-space indent, sorted keys, one array
// element per line, binary32 shortest round-trip floats, trailing newline.
std::expected<std::string, std::string> dump(const nlohmann::json& value);

// Shortest round-trip decimal text for a float value (binary32), "0.0" for
// any zero. Fails for NaN, infinity and doubles that overflow binary32.
std::expected<std::string, std::string> format_float(double value);

enum class Dialect { Legacy, V2 };

// Reads the format/version header of a standalone file. Legacy files carry
// only "format_version": 1; v2 files carry "format": <format_name> and
// "version": 2. Throws SchemaError for anything else, including future
// versions (never interpreted).
Dialect detect_dialect(const nlohmann::json& root, std::string_view format_name, std::string_view what);

[[noreturn]]  void fail(const std::string& message);
void require_object(const nlohmann::json& j, std::string_view context);
// Throws if `object` has a key that is not in `allowed`.
void require_only_keys(const nlohmann::json& object, std::initializer_list<std::string_view> allowed,
                       std::string_view context);

bool as_bool(const nlohmann::json& j, std::string_view context);
int as_int(const nlohmann::json& j, std::string_view context);
std::uint64_t as_uint(const nlohmann::json& j, std::string_view context);
float as_float(const nlohmann::json& j, std::string_view context);
std::string as_string(const nlohmann::json& j, std::string_view context);
Vec2 as_vec2(const nlohmann::json& j, std::string_view context);
Color as_color(const nlohmann::json& j, std::string_view context);

// Project-relative path: forward slashes only, no leading '/', drive
// letter, '.', '..' or empty segments. `allow_empty` for optional paths.
void require_project_path(const std::string& path, bool allow_empty, std::string_view context);

}  // namespace arcane::canonical
