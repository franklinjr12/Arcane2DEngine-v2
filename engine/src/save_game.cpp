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

// True when `text` is a well-formed save from a newer engine: refuse it, never fall back or overwrite.
bool is_newer_file_version(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed || !parsed->is_object() || !parsed->contains("version")) return false;
  const json& v = (*parsed)["version"];
  return v.is_number_integer() && v.get<long long>() > 2;
}

json meta_to_json(const SaveMeta& m) {
  return {{"display_name", m.display_name}, {"play_time_seconds", m.play_time_seconds}, {"timestamp", m.timestamp}};
}

SaveMeta meta_from_json(const json& j) {
  canonical::require_object(j, "save 'meta'");
  canonical::require_only_keys(j, {"display_name", "play_time_seconds", "timestamp"}, "save 'meta'");
  SaveMeta m;
  if (j.contains("display_name")) {
    if (!j["display_name"].is_string()) canonical::fail("save meta: 'display_name' must be a string");
    m.display_name = j["display_name"].get<std::string>();
  }
  if (j.contains("play_time_seconds")) {
    m.play_time_seconds = canonical::as_float(j["play_time_seconds"], "save meta 'play_time_seconds'");
    if (!(m.play_time_seconds >= 0.0f)) canonical::fail("save meta: play_time_seconds must be >= 0");
  }
  if (j.contains("timestamp")) {
    if (!j["timestamp"].is_number_integer()) canonical::fail("save meta: 'timestamp' must be an integer");
    m.timestamp = j["timestamp"].get<long long>();
  }
  return m;
}

// Header + meta of a well-formed current-version save; the values are not looked at.
std::expected<SaveSlotInfo, std::string> info_from_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("malformed save JSON: {}", parsed.error()));
  try {
    const json& root = *parsed;
    canonical::require_object(root, "save");
    canonical::require_only_keys(root, {"format", "version", "data_version", "values", "meta"}, "save");
    if (!root.contains("format") || root["format"] != kSaveFormat) canonical::fail("save: 'format' must be \"arcane.save\"");
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != 2) {
      canonical::fail("save: unsupported file 'version' (this engine reads 2)");
    }
    SaveSlotInfo info;
    info.file_version = 2;
    info.data_version = canonical::as_int(root.value("data_version", json(1)), "save 'data_version'");
    if (info.data_version < 1) canonical::fail("save: data_version must be >= 1");
    if (root.contains("meta")) info.meta = meta_from_json(root["meta"]);
    return info;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
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
  if (!data.meta.empty()) {
    if (!(data.meta.play_time_seconds >= 0.0f)) return std::unexpected(std::string("save: play_time_seconds must be >= 0"));
    root["meta"] = meta_to_json(data.meta);
  }
  return canonical::dump(root);
}

std::expected<SaveData, std::string> load_save_game_from_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("malformed save JSON: {}", parsed.error()));
  try {
    const json& root = *parsed;
    canonical::require_object(root, "save");
    canonical::require_only_keys(root, {"format", "version", "data_version", "values", "meta"}, "save");
    if (!root.contains("format") || root["format"] != kSaveFormat) canonical::fail("save: 'format' must be \"arcane.save\"");
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != 2) {
      canonical::fail("save: unsupported file 'version' (this engine reads 2)");
    }
    SaveData data;
    data.data_version = canonical::as_int(root.value("data_version", json(1)), "save 'data_version'");
    if (data.data_version < 1) canonical::fail("save: data_version must be >= 1");
    if (root.contains("meta")) data.meta = meta_from_json(root["meta"]);
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
  const auto path = save_file_path(dir, slot);
  if (std::filesystem::exists(path, ec)) {
    if (auto old = read_text_file(path)) {
      if (is_newer_file_version(*old)) {
        return std::unexpected(std::format("save: {} was written by a newer engine; not overwriting", path.string()));
      }
      if (load_save_game_from_json(*old)) {
        if (auto kept = write_text_file_atomic(save_backup_path(dir, slot), *old); !kept) {
          return std::unexpected(std::format("cannot keep backup: {}", kept.error()));
        }
      }
    }
  }
  return write_text_file_atomic(path, *text);
}

std::filesystem::path save_backup_path(const std::filesystem::path& dir, std::string_view slot) {
  return dir / (std::string(slot) + std::string(kSaveSuffix) + ".bak");
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

std::expected<LoadedSave, std::string> load_save_game_recovering(const std::filesystem::path& dir,
                                                                 std::string_view slot, int current_version,
                                                                 const SaveMigration& migrate) {
  if (!is_valid_save_slot(slot)) return std::unexpected(std::format("save: invalid slot name '{}'", slot));
  LoadedSave out;
  auto text = read_text_file(save_file_path(dir, slot));
  std::string why;
  if (text) {
    if (is_newer_file_version(*text)) {
      return std::unexpected(std::format("save slot '{}' was written by a newer engine and is not read", slot));
    }
    if (auto data = load_save_game_from_json(*text)) out.data = std::move(*data);
    else why = data.error();
  } else {
    why = text.error();
  }
  if (!why.empty()) {
    auto backup = read_text_file(save_backup_path(dir, slot));
    std::expected<SaveData, std::string> data =
        backup ? load_save_game_from_json(*backup) : std::unexpected(backup.error());
    if (!data) {
      return std::unexpected(std::format("save slot '{}' is unusable ({}) and so is its backup ({})", slot, why,
                                         data.error()));
    }
    out.data = std::move(*data);
    out.from_backup = true;
    out.recovery_note = std::move(why);
  }
  if (auto ok = migrate_save_game(out.data, current_version, migrate); !ok) return std::unexpected(ok.error());
  return out;
}

std::expected<void, std::string> delete_save_game(const std::filesystem::path& dir, std::string_view slot) {
  if (!is_valid_save_slot(slot)) return std::unexpected(std::format("save: invalid slot name '{}'", slot));
  for (const auto& path : {save_file_path(dir, slot), save_backup_path(dir, slot)}) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) return std::unexpected(std::format("cannot delete {}: {}", path.string(), ec.message()));
  }
  return {};
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

std::expected<SaveSlotInfo, std::string> read_save_slot_info(const std::filesystem::path& dir,
                                                             std::string_view slot) {
  if (!is_valid_save_slot(slot)) return std::unexpected(std::format("save: invalid slot name '{}'", slot));
  auto text = read_text_file(save_file_path(dir, slot));
  std::string why;
  if (text) {
    if (is_newer_file_version(*text)) {
      return std::unexpected(std::format("save slot '{}' was written by a newer engine and is not read", slot));
    }
    if (auto info = info_from_json(*text)) {
      info->slot = std::string(slot);
      return info;
    } else {
      why = info.error();
    }
  } else {
    why = text.error();
  }
  auto backup = read_text_file(save_backup_path(dir, slot));
  auto info = backup ? info_from_json(*backup) : std::unexpected(backup.error());
  if (!info) {
    return std::unexpected(std::format("save slot '{}' is unusable ({}) and so is its backup ({})", slot, why,
                                       info.error()));
  }
  info->slot = std::string(slot);
  info->from_backup = true;
  return info;
}

std::vector<SaveSlotInfo> list_save_slot_infos(const std::filesystem::path& dir) {
  std::vector<SaveSlotInfo> infos;
  for (const std::string& slot : list_save_slots(dir)) {
    if (auto info = read_save_slot_info(dir, slot)) {
      infos.push_back(std::move(*info));
    } else {
      SaveSlotInfo bad;
      bad.slot = slot;
      bad.error = info.error();
      infos.push_back(std::move(bad));
    }
  }
  std::sort(infos.begin(), infos.end(), [](const SaveSlotInfo& a, const SaveSlotInfo& b) {
    return a.meta.timestamp != b.meta.timestamp ? a.meta.timestamp < b.meta.timestamp : a.slot < b.slot;
  });
  return infos;
}

}  // namespace arcane
