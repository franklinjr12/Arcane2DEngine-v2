#include "arcane/save_game.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>

#include "arcane/canonical_json.hpp"
#include "arcane/project.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {

using nlohmann::json;

constexpr const char* kSaveFormat = "arcane.save";
constexpr std::string_view kSaveSuffix = ".save.json";

bool slot_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

const char* env(const char* name) {
  const char* value = std::getenv(name);
  return value && *value ? value : nullptr;
}

}  // namespace

const SaveValue* SaveData::find(std::string_view key) const noexcept {
  const auto it = values.find(std::string(key));
  return it == values.end() ? nullptr : &it->second;
}

bool SaveData::get_bool(std::string_view key, bool fallback) const noexcept {
  const auto* v = find(key);
  return v && std::holds_alternative<bool>(*v) ? std::get<bool>(*v) : fallback;
}

int SaveData::get_int(std::string_view key, int fallback) const noexcept {
  const auto* v = find(key);
  return v && std::holds_alternative<int>(*v) ? std::get<int>(*v) : fallback;
}

float SaveData::get_float(std::string_view key, float fallback) const noexcept {
  const auto* v = find(key);
  if (v && std::holds_alternative<float>(*v)) return std::get<float>(*v);
  if (v && std::holds_alternative<int>(*v)) return static_cast<float>(std::get<int>(*v));
  return fallback;
}

std::string SaveData::get_string(std::string_view key, std::string fallback) const {
  const auto* v = find(key);
  return v && std::holds_alternative<std::string>(*v) ? std::get<std::string>(*v) : std::move(fallback);
}

bool is_valid_save_key(std::string_view key) noexcept {
  if (key.empty() || key.size() > 128) return false;
  return std::all_of(key.begin(), key.end(), [](char c) { return slot_char(c) || c == '.' || c == ':' || c == '/'; });
}

bool is_valid_save_slot(std::string_view slot) noexcept {
  return !slot.empty() && slot.size() <= 64 && std::all_of(slot.begin(), slot.end(), slot_char);
}

std::filesystem::path save_directory(const std::string& game_id) {
  if (!is_valid_game_id(game_id)) return {};
  if (const char* base = env("ARCANE_SAVE_DIR")) return std::filesystem::path(base) / game_id;
#ifdef _WIN32
  if (const char* appdata = env("APPDATA")) return std::filesystem::path(appdata) / "Arcane" / game_id;
#else
  if (const char* xdg = env("XDG_DATA_HOME")) return std::filesystem::path(xdg) / "arcane" / game_id;
  if (const char* home = env("HOME")) return std::filesystem::path(home) / ".local" / "share" / "arcane" / game_id;
#endif
  return {};
}

std::filesystem::path save_file_path(const std::filesystem::path& dir, std::string_view slot) {
  return dir / (std::string(slot) + std::string(kSaveSuffix));
}

std::expected<std::string, std::string> save_game_canonical_json(const SaveData& data) {
  if (data.data_version < 1) return std::unexpected(std::string("save: data_version must be >= 1"));
  json values = json::object();
  for (const auto& [key, value] : data.values) {
    if (!is_valid_save_key(key)) return std::unexpected(std::format("save: invalid key '{}'", key));
    if (const auto* b = std::get_if<bool>(&value)) values[key] = *b;
    else if (const auto* i = std::get_if<int>(&value)) values[key] = *i;
    else if (const auto* f = std::get_if<float>(&value)) values[key] = *f;
    else values[key] = std::get<std::string>(value);
  }
  json root = {{"format", kSaveFormat}, {"version", 2}, {"data_version", data.data_version}, {"values", values}};
  return canonical::dump(root);
}

std::expected<SaveData, std::string> load_save_game_from_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("malformed save JSON: {}", parsed.error()));
  try {
    const json& root = *parsed;
    canonical::require_object(root, "save");
    canonical::require_only_keys(root, {"format", "version", "data_version", "values"}, "save");
    if (!root.contains("format") || root["format"] != kSaveFormat) canonical::fail("save: 'format' must be \"arcane.save\"");
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != 2) {
      canonical::fail("save: unsupported file 'version' (this engine reads 2)");
    }
    SaveData data;
    data.data_version = canonical::as_int(root.value("data_version", json(1)), "save 'data_version'");
    if (data.data_version < 1) canonical::fail("save: data_version must be >= 1");
    if (root.contains("values")) {
      const json& values = root["values"];
      canonical::require_object(values, "save 'values'");
      for (auto it = values.begin(); it != values.end(); ++it) {
        const std::string where = "save value '" + it.key() + "'";
        if (!is_valid_save_key(it.key())) canonical::fail("save: invalid key '" + it.key() + "'");
        const json& v = it.value();
        if (v.is_boolean()) data.values[it.key()] = v.get<bool>();
        else if (v.is_number_integer()) data.values[it.key()] = canonical::as_int(v, where);
        else if (v.is_number_float()) data.values[it.key()] = canonical::as_float(v, where);
        else if (v.is_string()) data.values[it.key()] = v.get<std::string>();
        else canonical::fail(where + ": expected a boolean, number or string");
      }
    }
    return data;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
}

std::expected<void, std::string> write_save_game(const std::filesystem::path& dir, std::string_view slot,
                                                 const SaveData& data) {
  if (!is_valid_save_slot(slot)) return std::unexpected(std::format("save: invalid slot name '{}'", slot));
  auto text = save_game_canonical_json(data);
  if (!text) return std::unexpected(text.error());
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return std::unexpected(std::format("cannot create save folder {}: {}", dir.string(), ec.message()));
  return write_text_file_atomic(save_file_path(dir, slot), *text);
}

std::expected<SaveData, std::string> read_save_game_file(const std::filesystem::path& dir, std::string_view slot) {
  if (!is_valid_save_slot(slot)) return std::unexpected(std::format("save: invalid slot name '{}'", slot));
  auto text = read_text_file(save_file_path(dir, slot));
  if (!text) return std::unexpected(text.error());
  return load_save_game_from_json(*text);
}

std::expected<void, std::string> migrate_save_game(SaveData& data, int current_version, const SaveMigration& migrate,
                                                   bool* migrated) {
  if (migrated) *migrated = false;
  if (data.data_version > current_version) {
    return std::unexpected(std::format("save has data_version {} but this game only understands up to {}",
                                       data.data_version, current_version));
  }
  while (data.data_version < current_version) {
    if (!migrate) {
      return std::unexpected(std::format("save has data_version {} (game is at {}) and no migration was provided",
                                         data.data_version, current_version));
    }
    if (auto step = migrate(data, data.data_version); !step) {
      return std::unexpected(std::format("migration from data_version {} failed: {}", data.data_version, step.error()));
    }
    ++data.data_version;
    if (migrated) *migrated = true;
  }
  return {};
}

std::expected<SaveData, std::string> load_save_game(const std::filesystem::path& dir, std::string_view slot,
                                                    int current_version, const SaveMigration& migrate) {
  auto data = read_save_game_file(dir, slot);
  if (!data) return data;
  if (auto ok = migrate_save_game(*data, current_version, migrate); !ok) return std::unexpected(ok.error());
  return data;
}

std::vector<std::string> list_save_slots(const std::filesystem::path& dir) {
  std::vector<std::string> slots;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (name.size() > kSaveSuffix.size() && name.ends_with(kSaveSuffix)) {
      const std::string slot = name.substr(0, name.size() - kSaveSuffix.size());
      if (is_valid_save_slot(slot)) slots.push_back(slot);
    }
  }
  std::sort(slots.begin(), slots.end());
  return slots;
}

}  // namespace arcane
