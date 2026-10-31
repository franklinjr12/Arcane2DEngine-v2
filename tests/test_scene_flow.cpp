// FE22: authored transition/save config in project.json, SceneManager scene
// switching with persistent nodes and the HUD fade, the save-game API (round
// trip, atomic write, migration) and the transition.* / save.* commands.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/project.hpp"
#include "arcane/save_game.hpp"
#include "arcane/scene_flow.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
using namespace arcane::tooling;
namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void spit(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

struct TempProject {
  fs::path dir = fs::temp_directory_path() / "arcane_scene_flow_test";
  ComponentRegistry registry;
  TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
    register_builtin_components(registry);
    auto session = editor::ProjectSession::create(dir, "Flow", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    ARCANE_ASSERT_TRUE(session->save(registry).has_value());

    Scene a;
    a.create_node("MenuRoot");
    a.create_node("Spare");
    spit(dir / "scenes/a.scene.json", a.save_to_json(registry));
    Scene b;
    Node* level = b.create_node("Level");
    b.create_node("Tile", level);
    spit(dir / "scenes/b.scene.json", b.save_to_json(registry));
  }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

Scene load_scene(const TempProject& p, const char* file) {
  auto scene = Scene::load_from_json(slurp(p.dir / "scenes" / file), p.registry);
  ARCANE_ASSERT_TRUE(scene.has_value());
  return std::move(*scene);
}

bool has_root(Scene& scene, const std::string& name) {
  for (const auto& r : scene.roots()) {
    if (r->name() == name) return true;
  }
  return false;
}

void test_project_config() {
  TempProject p;
  const std::string before = slurp(p.dir / "project.json");
  ARCANE_ASSERT_TRUE(before.find("transition") == std::string::npos && before.find("\"save\"") == std::string::npos);

  CommandRegistry registry;
  register_flow_commands(registry);
  auto context = ProjectContext::open(p.dir);
  ARCANE_ASSERT_TRUE(context.has_value());
  ProjectContext& ctx = **context;

  auto got = registry.execute(ctx, {"transition.get", Json::object()});
  ARCANE_ASSERT_TRUE(got.has_value() && got->value["fade_out_seconds"] == 0.3f);
  // Setting the same values changes nothing on disk.
  auto same = registry.execute(ctx, {"transition.set", {{"fade_out_seconds", 0.3}}}, {.persist = true});
  ARCANE_ASSERT_TRUE(same.has_value() && same->value["changed"] == false && !same->inverse);
  ARCANE_ASSERT_TRUE(slurp(p.dir / "project.json") == before);

  auto set = registry.execute(ctx, {"transition.set", {{"fade_out_seconds", 0.5}, {"color", Json::array({1, 0, 0, 1})}}}, {.persist = true});
  ARCANE_ASSERT_TRUE(set.has_value() && set->value["changed"] == true && set->inverse);
  const std::string after = slurp(p.dir / "project.json");
  ARCANE_ASSERT_TRUE(after.find("\"fade_out_seconds\": 0.5") != std::string::npos);
  ARCANE_ASSERT_TRUE(canonicalize_json(after).value() == after);
  auto project = load_project_from_json_strict(after);
  ARCANE_ASSERT_TRUE(project.has_value() && project->transition.fade_out_seconds == 0.5f && project->transition.color.r == 1.0f);
  ARCANE_ASSERT_TRUE(project->transition.fade_in_seconds == 0.3f);
  // The inverse restores the original bytes.
  ARCANE_ASSERT_TRUE(registry.execute(ctx, *set->inverse, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(slurp(p.dir / "project.json") == before);

  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"transition.set", {{"fade_in_seconds", 99}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"transition.set", {{"color", Json::array({1, 2})}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(slurp(p.dir / "project.json") == before);

  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"save.config_set", {{"game_id", "my.game"}, {"data_version", 3}}}, {.persist = true}).has_value());
  auto cfg = registry.execute(ctx, {"save.config_get", Json::object()});
  ARCANE_ASSERT_TRUE(cfg.has_value() && cfg->value["game_id"] == "my.game" && cfg->value["data_version"] == 3);
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.config_set", {{"game_id", "../evil"}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.config_set", {{"data_version", 0}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(load_project_from_json_strict(slurp(p.dir / "project.json")).has_value());
  // Unknown keys in the new sections are errors.
  std::string bad = slurp(p.dir / "project.json");
  bad.replace(bad.find("\"data_version\""), 14, "\"extra\": 1,\n    \"data_version\"");
  ARCANE_ASSERT_TRUE(!load_project_from_json_strict(bad).has_value());
}

void test_scene_manager() {
  TempProject p;
  SceneManager manager(p.dir, p.registry);

  // Startup scene with a persistent player (root + child, Camera2D targeting the child).
  Scene start = load_scene(p, "a.scene.json");
  Node* player = start.create_node("Player");
  Node* hand = start.create_node("Hand", player);
  Component* camera = start.add_component(player, "Camera2D", p.registry);
  ARCANE_ASSERT_TRUE(camera != nullptr);
  auto* cam = static_cast<Camera2D*>(camera);
  cam->target = hand->id();
  player->local_transform().position = {3.0f, 4.0f};
  manager.set_scene(std::move(start));
  const NodeId old_id = manager.scene()->roots().back()->id();
  ARCANE_ASSERT_TRUE(manager.set_persistent(old_id, true));
  ARCANE_ASSERT_TRUE(!manager.set_persistent(manager.scene()->roots().back()->children()[0]->id(), true));  // not a root
  ARCANE_ASSERT_TRUE(!manager.set_persistent(9999, true));

  // Immediate switch: persistent node kept with its state, others replaced.
  ARCANE_ASSERT_TRUE(manager.switch_now("scenes/b.scene.json").has_value());
  Scene& b = *manager.scene();
  ARCANE_ASSERT_TRUE(has_root(b, "Level") && has_root(b, "Player") && !has_root(b, "MenuRoot") && !has_root(b, "Spare"));
  Node* kept = manager.find_persistent("Player");
  ARCANE_ASSERT_TRUE(kept != nullptr && manager.is_persistent(kept->id()));
  ARCANE_ASSERT_TRUE(kept == b.find_node(kept->id()) && kept->local_transform().position.x == 3.0f);
  ARCANE_ASSERT_TRUE(kept->id() != old_id || b.next_id() > kept->id());
  // IDs are unique in the new scene and the camera target followed its child.
  auto* kept_cam = static_cast<Camera2D*>(kept->find_component("Camera2D"));
  ARCANE_ASSERT_TRUE(kept_cam == cam && kept_cam->target == kept->children()[0]->id());
  ARCANE_ASSERT_TRUE(b.find_component(kept_cam->id()) == kept_cam && b.find_node(kept_cam->id()) == nullptr);
  ARCANE_ASSERT_TRUE(manager.current_scene_path() == "scenes/b.scene.json");
  // Round trip proves the adopted tree is a valid scene.
  ARCANE_ASSERT_TRUE(Scene::load_from_json(b.save_to_json(p.registry), p.registry).has_value());

  // Failures keep the current scene and report why.
  ARCANE_ASSERT_TRUE(!manager.switch_now("scenes/missing.scene.json").has_value());
  ARCANE_ASSERT_TRUE(!manager.switch_now("../outside.json").has_value());
  ARCANE_ASSERT_TRUE(has_root(*manager.scene(), "Level"));

  // Fade: out 1s, switch at the end, in 0.5s. The overlay is a HUD bar.
  TransitionConfig cfg;
  cfg.fade_out_seconds = 1.0f;
  cfg.fade_in_seconds = 0.5f;
  cfg.color = Color{0.0f, 0.0f, 0.0f, 1.0f};
  manager.set_config(cfg);
  Hud hud;
  manager.set_hud(&hud);
  int switched = 0;
  manager.set_on_switched([&](Scene&) { ++switched; });
  manager.request_switch("scenes/a.scene.json");
  ARCANE_ASSERT_TRUE(manager.busy());
  manager.update(0.0f);
  ARCANE_ASSERT_TRUE(manager.phase() == SceneManager::Phase::FadingOut && manager.fade_alpha() == 0.0f);
  manager.update(0.5f);
  ARCANE_ASSERT_TRUE(manager.fade_alpha() == 0.5f && switched == 0 && has_root(*manager.scene(), "Level"));
  ARCANE_ASSERT_TRUE(hud.elements().size() == 1);
  const auto* bar = std::get_if<HudProgressBar>(&hud.elements()[0]);
  ARCANE_ASSERT_TRUE(bar && bar->id == "scene_fade" && bar->fill.a == 0.5f);
  manager.update(0.5f);  // fade out done: switch happens
  ARCANE_ASSERT_TRUE(switched == 1 && has_root(*manager.scene(), "MenuRoot") && !has_root(*manager.scene(), "Level"));
  ARCANE_ASSERT_TRUE(manager.find_persistent("Player") != nullptr);
  ARCANE_ASSERT_TRUE(manager.phase() == SceneManager::Phase::FadingIn && manager.fade_alpha() == 1.0f);
  manager.update(0.25f);
  ARCANE_ASSERT_TRUE(manager.fade_alpha() == 0.5f);
  manager.update(0.25f);
  ARCANE_ASSERT_TRUE(manager.phase() == SceneManager::Phase::Idle && manager.fade_alpha() == 0.0f && hud.elements().empty());
  ARCANE_ASSERT_TRUE(!manager.busy() && manager.last_error().empty());

  // No fade requested: switches at the next update; a bad target is reported and the scene stays.
  manager.request_switch("scenes/b.scene.json", false);
  manager.update(0.0f);
  ARCANE_ASSERT_TRUE(switched == 2 && manager.phase() == SceneManager::Phase::Idle && has_root(*manager.scene(), "Level"));
  manager.request_switch("scenes/missing.scene.json", false);
  manager.update(0.0f);
  ARCANE_ASSERT_TRUE(!manager.last_error().empty() && has_root(*manager.scene(), "Level") && switched == 2);

  // Zero-length fades behave like an immediate switch.
  manager.set_config(TransitionConfig{0.0f, 0.0f, {}});
  manager.request_switch("scenes/a.scene.json");
  manager.update(0.0f);
  ARCANE_ASSERT_TRUE(has_root(*manager.scene(), "MenuRoot") && !manager.busy());
  ARCANE_ASSERT_TRUE(manager.find_persistent("Player") != nullptr);
  // Un-persisting lets the node go on the next switch.
  ARCANE_ASSERT_TRUE(manager.set_persistent(manager.find_persistent("Player")->id(), false));
  ARCANE_ASSERT_TRUE(manager.switch_now("scenes/b.scene.json").has_value() && !has_root(*manager.scene(), "Player"));
}

void test_save_api() {
  const fs::path dir = fs::temp_directory_path() / "arcane_save_api_test";
  std::error_code ec;
  fs::remove_all(dir, ec);

  SaveData data;
  data.data_version = 2;
  data.set("player/name", std::string("Ana"));
  data.set("level", 3);
  data.set("volume", 0.75f);
  data.set("music", true);
  data.set("zero", 0.0f);
  auto text = save_game_canonical_json(data);
  ARCANE_ASSERT_TRUE(text.has_value() && canonicalize_json(*text).value() == *text);
  ARCANE_ASSERT_TRUE(text->find("\"zero\": 0.0") != std::string::npos && text->find("\"level\": 3,") != std::string::npos);
  auto back = load_save_game_from_json(*text);
  ARCANE_ASSERT_TRUE(back.has_value() && back->data_version == 2 && back->values.size() == 5);
  ARCANE_ASSERT_TRUE(back->get_int("level") == 3 && back->get_float("volume") == 0.75f && back->get_bool("music"));
  ARCANE_ASSERT_TRUE(back->get_string("player/name") == "Ana" && back->get_int("missing", 7) == 7);
  ARCANE_ASSERT_TRUE(std::holds_alternative<float>(*back->find("zero")) && std::holds_alternative<int>(*back->find("level")));
  ARCANE_ASSERT_TRUE(save_game_canonical_json(*back).value() == *text);

  // Strictness.
  auto broken = [&](const std::string& from, const std::string& to) {
    std::string t = *text;
    t.replace(t.find(from), from.size(), to);
    return load_save_game_from_json(t).has_value();
  };
  ARCANE_ASSERT_TRUE(!broken("\"version\": 2", "\"version\": 3"));
  ARCANE_ASSERT_TRUE(!broken("\"format\": \"arcane.save\"", "\"format\": \"arcane.scene\""));
  ARCANE_ASSERT_TRUE(!broken("\"values\"", "\"extra\": 1,\n  \"values\""));
  ARCANE_ASSERT_TRUE(!broken("\"level\": 3", "\"level\": [3]"));
  ARCANE_ASSERT_TRUE(!broken("\"level\"", "\"bad key\""));
  ARCANE_ASSERT_TRUE(!load_save_game_from_json("{").has_value());
  SaveData bad_key;
  bad_key.set("a b", 1);
  ARCANE_ASSERT_TRUE(!save_game_canonical_json(bad_key).has_value());

  // Slots, directory creation, listing.
  ARCANE_ASSERT_TRUE(is_valid_save_slot("slot_1") && !is_valid_save_slot("../x") && !is_valid_save_slot(""));
  ARCANE_ASSERT_TRUE(!write_save_game(dir, "../x", data).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(dir / "nested", "slot1", data).has_value());
  ARCANE_ASSERT_TRUE(write_save_game(dir / "nested", "auto", data).has_value());
  ARCANE_ASSERT_TRUE(list_save_slots(dir / "nested") == (std::vector<std::string>{"auto", "slot1"}));
  ARCANE_ASSERT_TRUE(list_save_slots(dir / "none").empty());
  ARCANE_ASSERT_TRUE(!fs::exists(staging_path(save_file_path(dir / "nested", "slot1"))));

  // Atomic write: when staging fails the old save is intact and unchanged.
  const fs::path file = save_file_path(dir / "nested", "slot1");
  const std::string original = slurp(file);
  fs::create_directory(staging_path(file));  // a directory squats on the temp name
  SaveData changed = data;
  changed.set("level", 99);
  ARCANE_ASSERT_TRUE(!write_save_game(dir / "nested", "slot1", changed).has_value());
  ARCANE_ASSERT_TRUE(slurp(file) == original);
  fs::remove(staging_path(file));
  ARCANE_ASSERT_TRUE(write_save_game(dir / "nested", "slot1", changed).has_value());
  ARCANE_ASSERT_TRUE(read_save_game_file(dir / "nested", "slot1")->get_int("level") == 99);
  ARCANE_ASSERT_TRUE(!fs::exists(staging_path(file)));
  // A corrupt file is reported, not half-loaded.
  spit(save_file_path(dir, "corrupt"), "{\"format\": \"arcane.save\"");
  ARCANE_ASSERT_TRUE(!read_save_game_file(dir, "corrupt").has_value());
  ARCANE_ASSERT_TRUE(!read_save_game_file(dir, "absent").has_value());

  // Migration hook: v1 -> v3 runs steps 1 and 2 in order.
  SaveData old;
  old.data_version = 1;
  old.set("hp", 10);
  ARCANE_ASSERT_TRUE(write_save_game(dir, "old", old).has_value());
  std::vector<int> steps;
  SaveMigration migrate = [&](SaveData& d, int from) -> std::expected<void, std::string> {
    steps.push_back(from);
    if (from == 1) {
      d.set("health", d.get_int("hp"));
      d.values.erase("hp");
    } else if (from == 2) {
      d.set("max_health", d.get_int("health") * 2);
    } else {
      return std::unexpected(std::string("unexpected"));
    }
    return {};
  };
  auto upgraded = load_save_game(dir, "old", 3, migrate);
  ARCANE_ASSERT_TRUE(upgraded.has_value() && upgraded->data_version == 3);
  ARCANE_ASSERT_TRUE(upgraded->get_int("health") == 10 && upgraded->get_int("max_health") == 20 && !upgraded->find("hp"));
  ARCANE_ASSERT_TRUE((steps == std::vector<int>{1, 2}));
  ARCANE_ASSERT_TRUE(read_save_game_file(dir, "old")->data_version == 1);  // file untouched until written back
  ARCANE_ASSERT_TRUE(!load_save_game(dir, "old", 3).has_value());          // older, no hook
  ARCANE_ASSERT_TRUE(load_save_game(dir, "old", 1).has_value());           // current version: nothing to do
  ARCANE_ASSERT_TRUE(!load_save_game(dir / "nested", "slot1", 1, migrate).has_value());  // saved by a newer game (v2)
  SaveMigration failing = [](SaveData&, int) -> std::expected<void, std::string> { return std::unexpected(std::string("boom")); };
  auto failed = load_save_game(dir, "old", 2, failing);
  ARCANE_ASSERT_TRUE(!failed.has_value() && failed.error().find("boom") != std::string::npos);

  ARCANE_ASSERT_TRUE(save_directory("bad/id").empty());
  const fs::path user_dir = save_directory("my_game");
  ARCANE_ASSERT_TRUE(user_dir.empty() || user_dir.filename() == "my_game");
  fs::remove_all(dir, ec);
}

void test_save_commands() {
  TempProject p;
  const fs::path dir = fs::temp_directory_path() / "arcane_save_cmd_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  CommandRegistry registry;
  register_flow_commands(registry);
  auto context = ProjectContext::open(p.dir);
  ARCANE_ASSERT_TRUE(context.has_value());
  ProjectContext& ctx = **context;
  const std::string d = dir.string();

  // Without game_id or dir there is no folder.
  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"save.list", Json::object()}).error().code == "VALUE_INVALID");

  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"save.config_set", {{"game_id", "cmd_test"}, {"data_version", 2}}}, {.persist = true}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.set", {{"slot", "s1"}, {"key", "level"}, {"value", 1}, {"dir", d}}}).has_value());  // no slot yet
  auto created = registry.execute(ctx, {"save.set", {{"slot", "s1"}, {"key", "level"}, {"value", 1}, {"create", true}, {"dir", d}}});
  ARCANE_ASSERT_TRUE(created.has_value() && created->value["previous"].is_null());
  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"save.set", {{"slot", "s1"}, {"key", "old_name"}, {"value", "x"}, {"dir", d}}}).has_value());
  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"save.set", {{"slot", "s1"}, {"key", "ratio"}, {"value", 0.5}, {"dir", d}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.set", {{"slot", "s1"}, {"key", "x"}, {"value", Json::array()}, {"dir", d}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.set", {{"slot", "s1"}, {"key", "bad key"}, {"value", 1}, {"dir", d}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.set", {{"slot", "../s"}, {"key", "k"}, {"value", 1}, {"create", true}, {"dir", d}}}).has_value());

  auto list = registry.execute(ctx, {"save.list", {{"dir", d}}});
  ARCANE_ASSERT_TRUE(list.has_value() && list->value["slots"].size() == 1 && list->value["slots"][0]["data_version"] == 2);
  auto all = registry.execute(ctx, {"save.get", {{"slot", "s1"}, {"dir", d}}});
  ARCANE_ASSERT_TRUE(all.has_value() && all->value["values"]["level"] == 1 && all->value["values"]["ratio"] == 0.5);
  auto one = registry.execute(ctx, {"save.get", {{"slot", "s1"}, {"key", "old_name"}, {"dir", d}}});
  ARCANE_ASSERT_TRUE(one.has_value() && one->value["value"] == "x");
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.get", {{"slot", "s1"}, {"key", "nope"}, {"dir", d}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.get", {{"slot", "ghost"}, {"dir", d}}}).has_value());

  // Declarative migration: slot is at 2; bring it to 4 with a rename, remove and a default.
  const std::string before = slurp(save_file_path(dir, "s1"));
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.migrate", {{"slot", "s1"}, {"to", 4}, {"rename", {{"missing", "z"}}}, {"dir", d}}}).has_value());
  ARCANE_ASSERT_TRUE(slurp(save_file_path(dir, "s1")) == before);  // failure writes nothing
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.migrate", {{"slot", "s1"}, {"to", 1}, {"dir", d}}}).has_value());  // no downgrade
  auto migrated = registry.execute(ctx, {"save.migrate",
                                         {{"slot", "s1"}, {"to", 4}, {"rename", {{"old_name", "name"}}},
                                          {"remove", Json::array({"ratio"})}, {"defaults", {{"lives", 3}, {"level", 99}}}, {"dir", d}}});
  ARCANE_ASSERT_TRUE(migrated.has_value() && migrated->value["from_version"] == 2 && migrated->value["data_version"] == 4);
  auto after = read_save_game_file(dir, "s1");
  ARCANE_ASSERT_TRUE(after.has_value() && after->data_version == 4 && after->get_string("name") == "x");
  ARCANE_ASSERT_TRUE(!after->find("old_name") && !after->find("ratio") && after->get_int("lives") == 3 && after->get_int("level") == 1);
  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"save.remove", {{"slot", "s1"}, {"key", "lives"}, {"dir", d}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"save.remove", {{"slot", "s1"}, {"key", "lives"}, {"dir", d}}}).has_value());
  fs::remove_all(dir, ec);
}

}  // namespace

int main() {
  test_project_config();
  test_scene_manager();
  test_save_api();
  test_save_commands();
  std::puts("test_scene_flow OK");
  return 0;
}
