#pragma once
// Minimal JSON Schema (draft 2020-12 subset) validator for the engine's own
// schemas/arcane.*.schema.json files. Supported keywords: type, properties,
// required, additionalProperties, items, minItems, maxItems, minimum,
// maximum, minLength, enum, const, oneOf, pattern, local $ref ("#/$defs/x"),
// $defs and the annotations $id, $schema, title, description. Any other
// keyword in a schema is reported as an error instead of being ignored.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <json.hpp>

namespace arcane::test {

struct SchemaError {
  std::string file;
  std::string pointer;  // RFC 6901; "" is the document root
  std::string message;

  std::string str() const { return file + ": " + pointer + ": " + message; }
};

namespace schema_detail {

using Json = nlohmann::json;
using Errors = std::vector<SchemaError>;

inline std::string escape_token(const std::string& token) {
  std::string out;
  for (const char c : token) {
    if (c == '~') out += "~0";
    else if (c == '/') out += "~1";
    else out += c;
  }
  return out;
}

inline bool is_integer(const Json& v) {
  if (v.is_number_integer()) return true;  // signed and unsigned
  if (v.is_number_float()) {
    const double d = v.get<double>();
    return std::isfinite(d) && std::floor(d) == d;
  }
  return false;
}

// Three-way numeric comparison that cannot overflow on unsigned 64-bit values.
inline int compare_numbers(const Json& a, const Json& b) {
  if (a.is_number_integer() && b.is_number_integer()) {
    const bool a_neg = a.is_number_integer() && !a.is_number_unsigned() && a.get<std::int64_t>() < 0;
    const bool b_neg = b.is_number_integer() && !b.is_number_unsigned() && b.get<std::int64_t>() < 0;
    if (a_neg != b_neg) return a_neg ? -1 : 1;
    if (a_neg) {
      const auto x = a.get<std::int64_t>(), y = b.get<std::int64_t>();
      return x < y ? -1 : (x > y ? 1 : 0);
    }
    const auto x = a.get<std::uint64_t>(), y = b.get<std::uint64_t>();
    return x < y ? -1 : (x > y ? 1 : 0);
  }
  const double x = a.get<double>(), y = b.get<double>();
  return x < y ? -1 : (x > y ? 1 : 0);
}

inline bool type_matches(const std::string& type, const Json& v) {
  if (type == "object") return v.is_object();
  if (type == "array") return v.is_array();
  if (type == "string") return v.is_string();
  if (type == "boolean") return v.is_boolean();
  if (type == "null") return v.is_null();
  if (type == "number") return v.is_number();
  if (type == "integer") return is_integer(v);
  return false;
}

inline const char* type_name(const Json& v) {
  if (v.is_object()) return "object";
  if (v.is_array()) return "array";
  if (v.is_string()) return "string";
  if (v.is_boolean()) return "boolean";
  if (v.is_null()) return "null";
  return "number";
}

inline bool is_annotation(const std::string& key) {
  return key == "$id" || key == "$schema" || key == "title" || key == "description" || key == "$defs";
}

class Validator {
 public:
  Validator(const Json& root, std::string file) : root_(root), file_(std::move(file)) {}

  void run(const Json& schema, const Json& value, const std::string& pointer, Errors& out) const {
    if (!schema.is_object()) {
      add(out, pointer, "schema node is not an object");
      return;
    }
    for (const auto& [key, arg] : schema.items()) {
      if (is_annotation(key)) continue;
      if (key == "$ref") {
        const Json* target = resolve(arg);
        if (target == nullptr) add(out, pointer, "unresolvable $ref " + arg.dump());
        else run(*target, value, pointer, out);
      } else if (key == "type") {
        check_type(arg, value, pointer, out);
      } else if (key == "const") {
        if (value != arg) add(out, pointer, "must equal " + arg.dump() + ", got " + value.dump());
      } else if (key == "enum") {
        if (std::find(arg.begin(), arg.end(), value) == arg.end()) {
          add(out, pointer, "must be one of " + arg.dump() + ", got " + value.dump());
        }
      } else if (key == "minimum" || key == "maximum") {
        if (value.is_number()) {
          const int c = compare_numbers(value, arg);
          if (key == "minimum" ? c < 0 : c > 0) {
            add(out, pointer, std::string(key == "minimum" ? "below minimum " : "above maximum ") + arg.dump() + ": " + value.dump());
          }
        }
      } else if (key == "minLength") {
        if (value.is_string() && utf8_length(value.get_ref<const std::string&>()) < arg.get<std::size_t>()) {
          add(out, pointer, "string shorter than " + arg.dump());
        }
      } else if (key == "pattern") {
        if (value.is_string()) {
          const std::regex re(arg.get<std::string>(), std::regex::ECMAScript);
          if (!std::regex_search(value.get_ref<const std::string&>(), re)) {
            add(out, pointer, "string " + value.dump() + " does not match pattern " + arg.dump());
          }
        }
      } else if (key == "minItems" || key == "maxItems") {
        if (value.is_array()) {
          const std::size_t limit = arg.get<std::size_t>();
          if (key == "minItems" ? value.size() < limit : value.size() > limit) {
            add(out, pointer, std::string(key == "minItems" ? "fewer" : "more") + " than " + arg.dump() + " items (has " + std::to_string(value.size()) + ")");
          }
        }
      } else if (key == "items") {
        if (value.is_array()) {
          for (std::size_t i = 0; i < value.size(); ++i) run(arg, value[i], pointer + "/" + std::to_string(i), out);
        }
      } else if (key == "required") {
        if (value.is_object()) {
          for (const auto& name : arg) {
            if (!value.contains(name.get<std::string>())) add(out, pointer, "missing required property \"" + name.get<std::string>() + "\"");
          }
        }
      } else if (key == "properties" || key == "additionalProperties") {
        // handled together below
      } else if (key == "oneOf") {
        check_one_of(arg, value, pointer, out);
      } else {
        add(out, pointer, "schema uses unsupported keyword \"" + key + "\"");
      }
    }
    if (value.is_object()) check_properties(schema, value, pointer, out);
  }

 private:
  const Json& root_;
  std::string file_;

  void add(Errors& out, const std::string& pointer, std::string message) const {
    out.push_back({file_, pointer, std::move(message)});
  }

  static std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (const unsigned char c : s) {
      if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
  }

  const Json* resolve(const Json& ref) const {
    if (!ref.is_string()) return nullptr;
    const std::string& r = ref.get_ref<const std::string&>();
    const std::string prefix = "#/$defs/";
    if (!r.starts_with(prefix) || !root_.contains("$defs")) return nullptr;
    const auto& defs = root_["$defs"];
    const std::string name = r.substr(prefix.size());
    return defs.contains(name) ? &defs[name] : nullptr;
  }

  void check_type(const Json& arg, const Json& value, const std::string& pointer, Errors& out) const {
    bool ok = false;
    if (arg.is_array()) {
      for (const auto& t : arg) ok = ok || type_matches(t.get<std::string>(), value);
    } else {
      ok = type_matches(arg.get<std::string>(), value);
    }
    if (!ok) add(out, pointer, "expected type " + arg.dump() + ", got " + type_name(value));
  }

  void check_properties(const Json& schema, const Json& value, const std::string& pointer, Errors& out) const {
    const Json* props = schema.contains("properties") ? &schema["properties"] : nullptr;
    const Json* extra = schema.contains("additionalProperties") ? &schema["additionalProperties"] : nullptr;
    for (const auto& [name, child] : value.items()) {
      const std::string child_pointer = pointer + "/" + escape_token(name);
      if (props != nullptr && props->contains(name)) {
        run((*props)[name], child, child_pointer, out);
      } else if (extra != nullptr) {
        if (extra->is_boolean()) {
          if (!extra->get<bool>()) add(out, child_pointer, "unknown property \"" + name + "\" (additionalProperties is false)");
        } else {
          run(*extra, child, child_pointer, out);
        }
      }
    }
  }

  void check_one_of(const Json& branches, const Json& value, const std::string& pointer, Errors& out) const {
    std::size_t matches = 0;
    std::size_t best = 0;
    Errors best_errors;
    for (std::size_t i = 0; i < branches.size(); ++i) {
      Errors branch_errors;
      run(branches[i], value, pointer, branch_errors);
      if (branch_errors.empty()) {
        ++matches;
      } else if (best_errors.empty() || branch_errors.size() < best_errors.size()) {
        best = i;
        best_errors = std::move(branch_errors);
      }
    }
    if (matches == 1) return;
    if (matches > 1) {
      add(out, pointer, "matches " + std::to_string(matches) + " oneOf branches, expected exactly 1");
      return;
    }
    add(out, pointer, "matches none of the " + std::to_string(branches.size()) + " oneOf branches; closest is branch " + std::to_string(best));
    for (auto& e : best_errors) out.push_back(std::move(e));
  }
};

inline bool read_text(const std::filesystem::path& path, std::string& text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  text = ss.str();
  return true;
}

inline void validate_document(const std::filesystem::path& schema_dir, const Json& doc, const std::string& file,
                              const std::string& pointer, Errors& out) {
  if (!doc.is_object() || !doc.contains("format") || !doc["format"].is_string()) {
    out.push_back({file, pointer, "document has no string \"format\" key"});
    return;
  }
  const std::string format = doc["format"].get<std::string>();
  const bool plain_name = !format.empty() && std::all_of(format.begin(), format.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || c == '.';
  });
  std::string schema_text;
  if (!plain_name || !read_text(schema_dir / (format + ".schema.json"), schema_text)) {
    out.push_back({file, pointer + "/format", "unknown format \"" + format + "\" (no schema)"});
    return;
  }
  const Json schema = Json::parse(schema_text, nullptr, false);
  if (schema.is_discarded()) {
    out.push_back({file, pointer, "schema for " + format + " is not valid JSON"});
    return;
  }
  const std::size_t before = out.size();
  Validator(schema, file).run(schema, doc, pointer, out);
  const bool clean = out.size() == before;

  if (format == "arcane.project" && clean) {
    for (const char* key : {"hud", "input_actions"}) {
      if (doc.contains(key) && doc[key].is_object()) {
        validate_document(schema_dir, doc[key], file, pointer + "/" + key, out);
      }
    }
  }
}

}  // namespace schema_detail

inline std::vector<SchemaError> validate(const nlohmann::json& schema, const nlohmann::json& doc, const std::string& file) {
  std::vector<SchemaError> errors;
  schema_detail::Validator(schema, file).run(schema, doc, "", errors);
  return errors;
}

// Parses `text`, picks schemas/<format>.schema.json from its "format" key, and
// also validates the hud / input_actions documents embedded in a project.
inline std::vector<SchemaError> validate_resource_text(const std::filesystem::path& schema_dir, const std::string& text,
                                                       const std::string& file) {
  std::vector<SchemaError> errors;
  const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
  if (doc.is_discarded()) {
    errors.push_back({file, "", "not valid JSON"});
    return errors;
  }
  schema_detail::validate_document(schema_dir, doc, file, "", errors);
  return errors;
}

}  // namespace arcane::test
