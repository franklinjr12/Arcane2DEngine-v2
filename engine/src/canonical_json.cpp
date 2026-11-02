#include "canonical_json.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "arcane/canonical_json.hpp"

namespace arcane::canonical {

namespace {

using nlohmann::json;

// Builds a json tree like the default parser but reports duplicate keys.
class StrictBuilder : public nlohmann::json_sax<json> {
 public:
  json root;
  std::string error;

  bool null() override { return put(json(nullptr)); }
  bool boolean(bool v) override { return put(json(v)); }
  bool number_integer(number_integer_t v) override { return put(json(v)); }
  bool number_unsigned(number_unsigned_t v) override { return put(json(v)); }
  bool number_float(number_float_t v, const string_t&) override { return put(json(v)); }
  bool string(string_t& v) override { return put(json(v)); }
  bool binary(binary_t&) override {
    error = "binary values are not allowed";
    return false;
  }
  bool start_object(std::size_t) override {
    if (!put(json::object())) return false;
    stack_.push_back(slot_);
    return true;
  }
  bool key(string_t& k) override {
    json& top = *stack_.back();
    if (top.contains(k)) {
      error = std::format("duplicate object key '{}'", k);
      return false;
    }
    key_ = &top[k];
    return true;
  }
  bool end_object() override {
    stack_.pop_back();
    return true;
  }
  bool start_array(std::size_t) override {
    if (!put(json::array())) return false;
    stack_.push_back(slot_);
    return true;
  }
  bool end_array() override {
    stack_.pop_back();
    return true;
  }
  bool parse_error(std::size_t position, const std::string&, const json::exception& ex) override {
    if (error.empty()) error = std::format("{} (at byte {})", ex.what(), position);
    return false;
  }

 private:
  bool put(json&& value) {
    if (stack_.empty()) {
      root = std::move(value);
      slot_ = &root;
    } else if (stack_.back()->is_array()) {
      stack_.back()->push_back(std::move(value));
      slot_ = &stack_.back()->back();
    } else {
      *key_ = std::move(value);
      slot_ = key_;
    }
    return true;
  }

  std::vector<json*> stack_;
  json* slot_ = nullptr;
  json* key_ = nullptr;
};

void write_value(const json& j, int depth, std::string& out);

void indent(int depth, std::string& out) { out.append(static_cast<std::size_t>(depth) * 2, ' '); }

void write_value(const json& j, int depth, std::string& out) {
  switch (j.type()) {
    case json::value_t::null:
      out += "null";
      break;
    case json::value_t::boolean:
      out += j.get<bool>() ? "true" : "false";
      break;
    case json::value_t::number_integer:
      out += std::to_string(j.get<std::int64_t>());
      break;
    case json::value_t::number_unsigned:
      out += std::to_string(j.get<std::uint64_t>());
      break;
    case json::value_t::number_float: {
      auto text = format_float(j.get<double>());
      if (!text) throw SchemaError(text.error());
      out += *text;
      break;
    }
    case json::value_t::string:
      try {
        out += j.dump();  // escaping only; throws on invalid UTF-8
      } catch (const json::exception&) {
        throw SchemaError("string is not valid UTF-8");
      }
      break;
    case json::value_t::array:
      if (j.empty()) {
        out += "[]";
        break;
      }
      out += "[\n";
      for (std::size_t i = 0; i < j.size(); ++i) {
        indent(depth + 1, out);
        write_value(j[i], depth + 1, out);
        out += i + 1 < j.size() ? ",\n" : "\n";
      }
      indent(depth, out);
      out += ']';
      break;
    case json::value_t::object: {
      if (j.empty()) {
        out += "{}";
        break;
      }
      out += "{\n";
      std::size_t i = 0;
      for (auto it = j.begin(); it != j.end(); ++it, ++i) {  // std::map: sorted by key bytes
        indent(depth + 1, out);
        out += json(it.key()).dump();
        out += ": ";
        write_value(it.value(), depth + 1, out);
        out += i + 1 < j.size() ? ",\n" : "\n";
      }
      indent(depth, out);
      out += '}';
      break;
    }
    default:
      throw SchemaError("unsupported JSON value (binary or discarded)");
  }
}

}  // namespace

std::expected<json, std::string> parse_strict(const std::string& text) {
  StrictBuilder builder;
  if (!json::sax_parse(text, &builder)) {
    return std::unexpected(builder.error.empty() ? std::string("malformed JSON") : builder.error);
  }
  return std::move(builder.root);
}

std::expected<std::string, std::string> format_float(double value) {
  if (!std::isfinite(value)) return std::unexpected(std::string("non-finite number (NaN/infinity) is not allowed"));
  const float f = static_cast<float>(value);
  if (!std::isfinite(f)) return std::unexpected(std::format("number {} is out of range for a 32-bit float", value));
  if (f == 0.0f) return std::string("0.0");  // also normalizes negative zero

  char buf[64];
  const auto res = std::to_chars(buf, buf + sizeof buf, f, std::chars_format::scientific);
  const std::string sci(buf, res.ptr);  // e.g. "-8.5e-01"
  std::size_t pos = 0;
  std::string sign;
  if (sci[0] == '-') {
    sign = "-";
    pos = 1;
  }
  const std::size_t e_pos = sci.find('e');
  std::string digits;
  for (std::size_t i = pos; i < e_pos; ++i) {
    if (sci[i] != '.') digits += sci[i];
  }
  const int exponent = std::stoi(sci.substr(e_pos + 1));
  const int n = static_cast<int>(digits.size());

  std::string out = sign;
  if (exponent >= 0 && exponent < 16) {
    if (n <= exponent + 1) {
      out += digits + std::string(static_cast<std::size_t>(exponent + 1 - n), '0') + ".0";
    } else {
      out += digits.substr(0, static_cast<std::size_t>(exponent) + 1) + "." +
             digits.substr(static_cast<std::size_t>(exponent) + 1);
    }
  } else if (exponent < 0 && exponent >= -5) {
    out += "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
  } else {
    out += digits.substr(0, 1);
    if (n > 1) out += "." + digits.substr(1);
    out += "e" + std::to_string(exponent);
  }
  return out;
}

std::expected<std::string, std::string> dump(const json& value) {
  std::string out;
  try {
    write_value(value, 0, out);
  } catch (const SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
  out += '\n';
  return out;
}

void fail(const std::string& message) { throw SchemaError(message); }

Dialect detect_dialect(const json& root, std::string_view format_name, std::string_view what) {
  if (!root.is_object()) fail(std::format("{} JSON must be an object", what));
  if (root.contains("format")) {
    if (!root["format"].is_string() || root["format"].get<std::string>() != format_name) {
      fail(std::format("{} JSON has format {}, expected \"{}\"", what, root["format"].dump(), format_name));
    }
    if (!root.contains("version") || !root["version"].is_number_integer()) {
      fail(std::format("{} JSON is missing an integer 'version' field", what));
    }
    const std::int64_t version = root["version"].get<std::int64_t>();
    if (version > 2) {
      fail(std::format("unsupported {} version {} (this build supports up to 2; file is newer than supported)", what,
                       version));
    }
    if (version != 2) fail(std::format("unsupported {} version {} (v2 files use version 2)", what, version));
    return Dialect::V2;
  }
  if (!root.contains("format_version") || !root["format_version"].is_number_integer()) {
    fail(std::format("{} JSON is missing a numeric 'format_version' field (or 'format'/'version' for v2)", what));
  }
  const std::int64_t version = root["format_version"].get<std::int64_t>();
  if (version != 1) {
    fail(std::format("unsupported {} format_version {} (this build only supports version 1; {} than supported)", what,
                     version, version > 1 ? "newer" : "older"));
  }
  return Dialect::Legacy;
}

void require_object(const json& j, std::string_view context) {
  if (!j.is_object()) fail(std::format("{}: expected an object", context));
}

void require_only_keys(const json& object, std::initializer_list<std::string_view> allowed,
                       std::string_view context) {
  for (auto it = object.begin(); it != object.end(); ++it) {
    bool known = false;
    for (std::string_view name : allowed) known = known || name == it.key();
    if (!known) fail(std::format("{}: unknown field '{}'", context, it.key()));
  }
}

bool as_bool(const json& j, std::string_view context) {
  if (!j.is_boolean()) fail(std::format("{}: expected a boolean", context));
  return j.get<bool>();
}

int as_int(const json& j, std::string_view context) {
  if (!j.is_number_integer()) fail(std::format("{}: expected an integer", context));
  if (j.is_number_unsigned() ? j.get<std::uint64_t>() > static_cast<std::uint64_t>(INT32_MAX)
                             : (j.get<std::int64_t>() < INT32_MIN || j.get<std::int64_t>() > INT32_MAX)) {
    fail(std::format("{}: integer out of range", context));
  }
  return j.get<int>();
}

std::uint64_t as_uint(const json& j, std::string_view context) {
  if (!j.is_number_integer() || (!j.is_number_unsigned() && j.get<std::int64_t>() < 0)) {
    fail(std::format("{}: expected a non-negative integer", context));
  }
  return j.get<std::uint64_t>();
}

float as_float(const json& j, std::string_view context) {
  if (!j.is_number()) fail(std::format("{}: expected a number", context));
  const double d = j.get<double>();
  if (!std::isfinite(d) || !std::isfinite(static_cast<float>(d))) fail(std::format("{}: number out of range", context));
  return static_cast<float>(d);
}

std::string as_string(const json& j, std::string_view context) {
  if (!j.is_string()) fail(std::format("{}: expected a string", context));
  return j.get<std::string>();
}

Vec2 as_vec2(const json& j, std::string_view context) {
  if (!j.is_array() || j.size() != 2) fail(std::format("{}: expected an array of 2 numbers", context));
  return Vec2{as_float(j[0], context), as_float(j[1], context)};
}

Color as_color(const json& j, std::string_view context) {
  if (!j.is_array() || j.size() != 4) fail(std::format("{}: expected an array of 4 numbers", context));
  return Color{as_float(j[0], context), as_float(j[1], context), as_float(j[2], context), as_float(j[3], context)};
}

void require_project_path(const std::string& path, bool allow_empty, std::string_view context) {
  if (path.empty()) {
    if (allow_empty) return;
    fail(std::format("{}: path must not be empty", context));
  }
  if (path.find('\\') != std::string::npos) fail(std::format("{}: path '{}' must use '/' separators", context, path));
  if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) {
    fail(std::format("{}: path '{}' must be project-relative", context, path));
  }
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    const std::string_view segment = std::string_view(path).substr(start, end - start);
    if (segment.empty() || segment == "." || segment == "..") {
      fail(std::format("{}: path '{}' has an empty, '.' or '..' segment", context, path));
    }
    start = end + 1;
  }
}

}  // namespace arcane::canonical

namespace arcane {

std::expected<std::string, std::string> canonicalize_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(parsed.error());
  return canonical::dump(*parsed);
}

// ---- files ---------------------------------------------------------------

std::expected<std::string, std::string> read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::unexpected(std::format("failed to open '{}' for reading", path.string()));
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::expected<void, std::string> write_text_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return std::unexpected(std::format("failed to open '{}' for writing", path.string()));
  out << text;
  out.flush();
  const bool ok = static_cast<bool>(out);
  out.close();  // must be closed before the file can be renamed or removed on Windows
  if (!ok) return std::unexpected(std::format("failed writing '{}'", path.string()));
  return {};
}

std::filesystem::path staging_path(const std::filesystem::path& path) {
  std::filesystem::path staged = path;
  staged += ".arcane-tmp";
  return staged;
}

std::expected<void, std::string> commit_staged_file(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::rename(staging_path(path), path, ec);
  if (ec) {
    std::filesystem::remove(staging_path(path), ec);
    return std::unexpected(std::format("failed to replace '{}'", path.string()));
  }
  return {};
}

std::expected<void, std::string> stage_text_file(const std::filesystem::path& path, const std::string& text) {
  auto written = write_text_file(staging_path(path), text);
  if (!written) {
    std::error_code ec;
    std::filesystem::remove(staging_path(path), ec);
  }
  return written;
}

std::expected<void, std::string> write_text_file_atomic(const std::filesystem::path& path, const std::string& text) {
  auto staged = stage_text_file(path, text);
  if (!staged) return staged;
  return commit_staged_file(path);
}

std::expected<void, std::string> write_text_files_transactional(const std::vector<FileWrite>& files,
                                                                const FileTransactionOptions& options) {
  std::vector<std::optional<std::string>> previous;
  for (const auto& file : files) {
    std::error_code ec;
    if (std::filesystem::exists(file.path, ec)) {
      auto old_text = read_text_file(file.path);
      if (!old_text) return std::unexpected(old_text.error());
      previous.push_back(std::move(*old_text));
    } else {
      previous.push_back(std::nullopt);
    }
  }
  auto discard_staged = [&](std::size_t from) {
    std::error_code ec;
    for (std::size_t i = from; i < files.size(); ++i) std::filesystem::remove(staging_path(files[i].path), ec);
  };
  for (const auto& file : files) {
    if (auto staged = stage_text_file(file.path, file.text); !staged) {
      discard_staged(0);
      return staged;
    }
  }
  for (std::size_t i = 0; i < files.size(); ++i) {
    std::expected<void, std::string> committed;
    if (static_cast<int>(i) == options.fail_commit_index) {
      committed = std::unexpected(std::format("simulated failure committing '{}'", files[i].path.string()));
    } else {
      committed = commit_staged_file(files[i].path);
    }
    if (!committed) {
      discard_staged(i);
      for (std::size_t j = 0; j < i; ++j) {
        if (previous[j]) {
          (void)write_text_file_atomic(files[j].path, *previous[j]);
        } else {
          std::error_code ec;
          std::filesystem::remove(files[j].path, ec);
        }
      }
      return std::unexpected(std::format("rolled back: {}", committed.error()));
    }
  }
  return {};
}

}  // namespace arcane
