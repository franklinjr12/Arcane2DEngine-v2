// Save slots: listing/deleting, data_version upgrade, .bak recovery, future-version refusal, no temp leftovers.
#include <filesystem>
#include <fstream>
#include <string>

#include "arcane/canonical_json.hpp"
#include "arcane/save_game.hpp"
#include "test_assert.hpp"

namespace {

namespace fs = std::filesystem;
using namespace arcane;

struct Dir {
  fs::path path = fs::temp_directory_path() / "arcane_test_save_game";
  Dir() { fs::remove_all(path); }
  ~Dir() { fs::remove_all(path); }
};

void put(const fs::path& p, const std::string& text) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << text;
}

std::string slurp(const fs::path& p) {
  auto t = read_text_file(p);
  ARCANE_ASSERT_TRUE(t.has_value());
  return *t;
}

SaveData sample(int level) {
  SaveData d;
  d.set("level", level);
  d.set("name", std::string("hero"));
  return d;
}

void no_leftovers(const fs::path& dir) {
  for (const auto& e : fs::directory_iterator(dir)) {
    ARCANE_ASSERT_TRUE(e.path().extension() != ".arcane-tmp");
    ARCANE_ASSERT_TRUE(e.path().filename() != ".arcane-commit");
  }
}

void test_list_and_delete() {
  Dir d;
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_2", sample(2)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", sample(1)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", sample(3)).has_value());  // creates slot_1's .bak
  ARCANE_ASSERT_TRUE(fs::exists(save_backup_path(d.path, "slot_1")));
  const auto slots = list_save_slots(d.path);  // the .bak is not a slot
  ARCANE_ASSERT_TRUE((slots == std::vector<std::string>{"slot_1", "slot_2"}));
  ARCANE_ASSERT_TRUE(delete_save_game(d.path, "slot_1").has_value());
  ARCANE_ASSERT_TRUE(!fs::exists(save_backup_path(d.path, "slot_1")));
  ARCANE_ASSERT_TRUE((list_save_slots(d.path) == std::vector<std::string>{"slot_2"}));
  ARCANE_ASSERT_TRUE(delete_save_game(d.path, "slot_1").has_value());  // already gone
  ARCANE_ASSERT_TRUE(!delete_save_game(d.path, "../x").has_value());
  no_leftovers(d.path);
}

void test_version_upgrade() {
  Dir d;
  SaveData old = sample(1);
  old.data_version = 1;
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", old).has_value());
  const SaveMigration up = [](SaveData& data, int from) -> std::expected<void, std::string> {
    if (from == 1) data.set("gold", 10);
    if (from == 2) data.set("gold", data.get_int("gold") * 2);
    return {};
  };
  auto loaded = load_save_game_recovering(d.path, "slot_1", 3, up);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->data.data_version == 3 && loaded->data.get_int("gold") == 20 && !loaded->from_backup);
  // data newer than the game: refused, file untouched
  SaveData future = sample(1);
  future.data_version = 5;
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_2", future).has_value());
  const std::string before = slurp(save_file_path(d.path, "slot_2"));
  ARCANE_ASSERT_TRUE(!load_save_game_recovering(d.path, "slot_2", 3, up).has_value());
  ARCANE_ASSERT_TRUE(slurp(save_file_path(d.path, "slot_2")) == before);
}

void test_truncated_falls_back_to_bak() {
  Dir d;
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", sample(1)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", sample(2)).has_value());
  const auto file = save_file_path(d.path, "slot_1");
  const std::string good = slurp(file);
  put(file, good.substr(0, good.size() / 2));
  auto loaded = load_save_game_recovering(d.path, "slot_1", 1);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->from_backup && !loaded->recovery_note.empty());
  ARCANE_ASSERT_TRUE(loaded->data.get_int("level") == 1);
  // a corrupt file never replaces the good .bak
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", sample(3)).has_value());
  ARCANE_ASSERT_TRUE(load_save_game_from_json(slurp(save_backup_path(d.path, "slot_1")))->get_int("level") == 1);
  loaded = load_save_game_recovering(d.path, "slot_1", 1);
  ARCANE_ASSERT_TRUE(loaded.has_value() && !loaded->from_backup && loaded->data.get_int("level") == 3);
  no_leftovers(d.path);
}

void test_garbage_file() {
  Dir d;
  fs::create_directories(d.path);
  put(save_file_path(d.path, "slot_1"), "\x01\x02 not json");
  ARCANE_ASSERT_TRUE(!load_save_game_recovering(d.path, "slot_1", 1).has_value());  // no .bak
  put(save_backup_path(d.path, "slot_1"), "also garbage");
  ARCANE_ASSERT_TRUE(!load_save_game_recovering(d.path, "slot_1", 1).has_value());
  ARCANE_ASSERT_TRUE(slurp(save_file_path(d.path, "slot_1")) == "\x01\x02 not json");
  // a missing slot file with a good .bak recovers too
  Dir e;
  ARCANE_ASSERT_TRUE(write_save_game(e.path, "slot_1", sample(1)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(e.path, "slot_1", sample(2)).has_value());
  fs::remove(save_file_path(e.path, "slot_1"));
  auto loaded = load_save_game_recovering(e.path, "slot_1", 1);
  ARCANE_ASSERT_TRUE(loaded.has_value() && loaded->from_backup);
}

void test_future_file_version_refused() {
  Dir d;
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", sample(1)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "slot_1", sample(2)).has_value());  // good .bak exists
  const auto file = save_file_path(d.path, "slot_1");
  const std::string future = R"({"format":"arcane.save","version":3,"data_version":1,"values":{}})";
  put(file, future);
  ARCANE_ASSERT_TRUE(!load_save_game_recovering(d.path, "slot_1", 1).has_value());  // no silent fallback
  const std::string bak = slurp(save_backup_path(d.path, "slot_1"));
  ARCANE_ASSERT_TRUE(!write_save_game(d.path, "slot_1", sample(9)).has_value());
  ARCANE_ASSERT_TRUE(slurp(file) == future);
  ARCANE_ASSERT_TRUE(slurp(save_backup_path(d.path, "slot_1")) == bak);
  no_leftovers(d.path);
}

SaveData with_meta(int level, const std::string& name, float play, long long ts) {
  SaveData d = sample(level);
  d.meta = {name, play, ts};
  return d;
}

void test_meta_round_trip_and_compat() {
  Dir d;
  // no meta: output has no "meta" key and loads with empty meta
  auto plain = save_game_canonical_json(sample(1));
  ARCANE_ASSERT_TRUE(plain.has_value() && plain->find("meta") == std::string::npos);
  ARCANE_ASSERT_TRUE(load_save_game_from_json(*plain)->meta.empty());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "a", with_meta(4, "Forest, night", 12.5f, 1700000000)).has_value());
  auto full = read_save_game_file(d.path, "a");
  ARCANE_ASSERT_TRUE(full.has_value() && full->get_int("level") == 4);
  ARCANE_ASSERT_TRUE(full->meta.display_name == "Forest, night" && full->meta.play_time_seconds == 12.5f &&
                     full->meta.timestamp == 1700000000);
  auto info = read_save_slot_info(d.path, "a");
  ARCANE_ASSERT_TRUE(info.has_value() && !info->from_backup && info->slot == "a" && info->data_version == 1);
  ARCANE_ASSERT_TRUE(info->meta.display_name == "Forest, night" && info->meta.timestamp == 1700000000);
  // bad meta is rejected by the strict loader
  ARCANE_ASSERT_TRUE(!load_save_game_from_json(
      R"({"format":"arcane.save","version":2,"data_version":1,"values":{},"meta":{"bogus":1}})").has_value());
  ARCANE_ASSERT_TRUE(!load_save_game_from_json(
      R"({"format":"arcane.save","version":2,"data_version":1,"values":{},"meta":{"timestamp":1.5}})").has_value());
  ARCANE_ASSERT_TRUE(!write_save_game(d.path, "b", with_meta(1, "x", -1.0f, 0)).has_value());
}

void test_listing_order() {
  Dir d;
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "zeta", with_meta(1, "Z", 1.0f, 100)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "beta", with_meta(1, "B", 1.0f, 200)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "alpha", with_meta(1, "A", 1.0f, 200)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "none", sample(1)).has_value());  // timestamp 0
  put(save_file_path(d.path, "broken"), "garbage");
  const auto infos = list_save_slot_infos(d.path);
  std::vector<std::string> order;
  for (const auto& i : infos) order.push_back(i.slot);
  ARCANE_ASSERT_TRUE((order == std::vector<std::string>{"broken", "none", "zeta", "alpha", "beta"}));
  ARCANE_ASSERT_TRUE(!infos[0].error.empty() && infos[1].error.empty() && infos[2].meta.display_name == "Z");
}

void test_meta_survives_bak() {
  Dir d;
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "s", with_meta(1, "old", 5.0f, 10)).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(d.path, "s", with_meta(2, "new", 9.0f, 20)).has_value());
  auto info = read_save_slot_info(d.path, "s");
  ARCANE_ASSERT_TRUE(info.has_value() && !info->from_backup && info->meta.display_name == "new");
  const auto file = save_file_path(d.path, "s");
  const std::string good = slurp(file);
  put(file, good.substr(0, good.size() / 2));
  info = read_save_slot_info(d.path, "s");
  ARCANE_ASSERT_TRUE(info.has_value() && info->from_backup);
  ARCANE_ASSERT_TRUE(info->meta.display_name == "old" && info->meta.timestamp == 10 && info->meta.play_time_seconds == 5.0f);
  auto loaded = load_save_game_recovering(d.path, "s", 1);
  ARCANE_ASSERT_TRUE(loaded.has_value() && loaded->from_backup && loaded->data.meta.display_name == "old");
  ARCANE_ASSERT_TRUE(list_save_slot_infos(d.path).at(0).from_backup);
  // newer engine file: error, no fallback
  put(file, R"({"format":"arcane.save","version":3})");
  ARCANE_ASSERT_TRUE(!read_save_slot_info(d.path, "s").has_value());
  ARCANE_ASSERT_TRUE(!read_save_slot_info(d.path, "../x").has_value());
  // meta is read without validating values
  put(file, R"({"format":"arcane.save","version":2,"data_version":3,"values":{"bad key":[1]},"meta":{"timestamp":7}})");
  info = read_save_slot_info(d.path, "s");
  ARCANE_ASSERT_TRUE(info.has_value() && !info->from_backup && info->data_version == 3 && info->meta.timestamp == 7);
}

}  // namespace

int main() {
  test_meta_round_trip_and_compat();
  test_listing_order();
  test_meta_survives_bak();
  test_list_and_delete();
  test_version_upgrade();
  test_truncated_falls_back_to_bak();
  test_garbage_file();
  test_future_file_version_refused();
  return 0;
}
