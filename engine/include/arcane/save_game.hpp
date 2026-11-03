#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "arcane/export.hpp"

namespace arcane {

// Save-game data (docs/formats/save.md): a flat, sorted key/value map plus the
// game's own data version. Files are canonical `arcane.save` v2 JSON, so they
// can be read and edited as text. Floats are binary32, like everywhere else.
using SaveValue = std::variant<bool, int, float, std::string>;

// Slot metadata, stored in the optional "meta" object of the file and readable without the values. The caller
// injects `timestamp` (e.g. Unix seconds) so saves stay deterministic; the engine never reads a clock.
struct SaveMeta {
  std::string display_name;
  float play_time_seconds = 0.0f;
  long long timestamp = 0;
  bool empty() const noexcept { return display_name.empty() && play_time_seconds == 0.0f && timestamp == 0; }
};

struct ARCANE_API SaveData {
  int data_version = 1;  // the game's save layout version, not the file format version
  SaveMeta meta;         // written only when not empty
  std::map<std::string, SaveValue> values;

  void set(std::string key, SaveValue value) { values[std::move(key)] = std::move(value); }
  const SaveValue* find(std::string_view key) const noexcept;
  bool get_bool(std::string_view key, bool fallback = false) const noexcept;
  int get_int(std::string_view key, int fallback = 0) const noexcept;
  float get_float(std::string_view key, float fallback = 0.0f) const noexcept;
  std::string get_string(std::string_view key, std::string fallback = {}) const;
};

// Keys: 1-128 characters of A-Z a-z 0-9 . _ - : /. Slots: 1-64 of A-Z a-z 0-9 _ -.
ARCANE_API bool is_valid_save_key(std::string_view key) noexcept;
ARCANE_API bool is_valid_save_slot(std::string_view slot) noexcept;

// Per-user folder for `game_id`: $ARCANE_SAVE_DIR/<game_id> if that variable is
// set, else %APPDATA%\Arcane\<game_id> (Windows), $XDG_DATA_HOME/arcane/<game_id>
// or ~/.local/share/arcane/<game_id>. Not created here. Empty if no base is known.
ARCANE_API std::filesystem::path save_directory(const std::string& game_id);
// "<dir>/<slot>.save.json"
ARCANE_API std::filesystem::path save_file_path(const std::filesystem::path& dir, std::string_view slot);

ARCANE_API std::expected<std::string, std::string> save_game_canonical_json(const SaveData& data);
// Strict: unknown fields, a missing/other format, an unsupported file version, wrong value types and invalid keys
// are errors.
ARCANE_API std::expected<SaveData, std::string> load_save_game_from_json(const std::string& text);

// "<dir>/<slot>.save.json.bak": the previous good save of the slot.
ARCANE_API std::filesystem::path save_backup_path(const std::filesystem::path& dir, std::string_view slot);

// Creates `dir` if needed and replaces `<dir>/<slot>.save.json` atomically
// (temp file, close, rename): a failed write leaves the old save untouched. If the old file is a valid save
// it is first kept as the .bak (a corrupt old file never replaces a good .bak). A file written by a newer
// engine (unsupported file 'version') is refused and left as it is.
ARCANE_API std::expected<void, std::string> write_save_game(const std::filesystem::path& dir, std::string_view slot,
                                                            const SaveData& data);
ARCANE_API std::expected<SaveData, std::string> read_save_game_file(const std::filesystem::path& dir,
                                                                    std::string_view slot);

// Upgrades one version step: turn data saved at `from_version` into
// `from_version + 1` layout (rename keys, add defaults, ...).
using SaveMigration = std::function<std::expected<void, std::string>(SaveData& data, int from_version)>;

// Brings `data` up to `current_version` by calling `migrate` once per step and
// updating data.data_version. Fails if the data is newer than the game, or
// older with no `migrate`, or a step fails (data may be partly migrated then;
// callers migrate a copy). Sets *migrated when any step ran.
ARCANE_API std::expected<void, std::string> migrate_save_game(SaveData& data, int current_version,
                                                              const SaveMigration& migrate, bool* migrated = nullptr);

// read_save_game_file + migrate_save_game. The file on disk is not rewritten;
// call write_save_game to keep the upgraded data.
ARCANE_API std::expected<SaveData, std::string> load_save_game(const std::filesystem::path& dir, std::string_view slot,
                                                               int current_version, const SaveMigration& migrate = {});

// A save loaded by load_save_game_recovering.
struct LoadedSave {
  SaveData data;
  bool from_backup = false;   // the slot file was unusable and the .bak was read instead
  std::string recovery_note;  // why the slot file was rejected (empty unless from_backup); a recoverable error
};

// Like load_save_game, but a slot file that is missing, truncated, not valid JSON, or fails the strict format
// check falls back to <slot>.save.json.bak and reports the reason in recovery_note. Nothing is rewritten.
// Not recoverable (error, .bak not used): a file from a newer engine (file 'version' > 2), data newer than
// `current_version`, or a failing migration step.
ARCANE_API std::expected<LoadedSave, std::string> load_save_game_recovering(const std::filesystem::path& dir,
                                                                            std::string_view slot, int current_version,
                                                                            const SaveMigration& migrate = {});

// Removes the slot file and its .bak. Not an error if neither exists.
ARCANE_API std::expected<void, std::string> delete_save_game(const std::filesystem::path& dir, std::string_view slot);

// Slot names that have a save file in `dir`, sorted. Empty if `dir` is missing.
ARCANE_API std::vector<std::string> list_save_slots(const std::filesystem::path& dir);

// What a save menu needs about one slot, without loading its values.
struct SaveSlotInfo {
  std::string slot;
  SaveMeta meta;
  int file_version = 2;     // the 'version' of the file the info came from
  int data_version = 1;
  bool from_backup = false;  // the slot file was unusable; the info is from the .bak
  std::string error;         // non-empty if neither the slot file nor the .bak could be read (other fields unset)
};

// Reads only the header and "meta" of the slot file (values are not validated); falls back to the .bak like
// load_save_game_recovering. A file from a newer engine is an error and does not fall back.
ARCANE_API std::expected<SaveSlotInfo, std::string> read_save_slot_info(const std::filesystem::path& dir,
                                                                        std::string_view slot);

// Info for every slot in `dir`, sorted by meta.timestamp (oldest first), then slot name. Slots that cannot be
// read are kept, with `error` set and timestamp 0.
ARCANE_API std::vector<SaveSlotInfo> list_save_slot_infos(const std::filesystem::path& dir);

}  // namespace arcane
