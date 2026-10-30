// Prefabs: save a subtree, instantiate it through the command registry,
// overrides as explicit diffs, break/relink, ID uniqueness and remapping,
// persistence across reopen, runtime spawn, and refusal of bad links.
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/module.hpp"
#include "arcane/prefab.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane::tooling;
namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

constexpr const char* kAsset = "prefabs/rig.prefab.json";

struct TempProject {
  fs::path dir = fs::temp_directory_path() / "arcane_prefab_test";
  arcane::NodeId child_id = 0;
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  // Node 1 "Rig" (Camera2D, Sprite, Spinner) with child "Hand" (Sprite).
  TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
    ARCANE_ASSERT_TRUE(module.has_value());
    {
      arcane::ComponentRegistry registry;
      arcane::register_builtin_components(registry);
      module->api()->register_types(registry);
      auto session = arcane::editor::ProjectSession::create(dir, "Prefab", registry);
      ARCANE_ASSERT_TRUE(session.has_value());
      arcane::Node* rig = session->scene().create_node("Rig");
      session->scene().add_component(rig, "Camera2D", registry);
      session->scene().add_component(rig, "Sprite", registry);
      session->scene().add_component(rig, "Spinner", registry);
      arcane::Node* hand = session->scene().create_node("Hand", rig);
      session->scene().add_component(hand, "Sprite", registry);
      child_id = hand->id();
      ARCANE_ASSERT_TRUE(session->save(registry).has_value());
    }
  }
};

std::unique_ptr<ProjectContext> open(const TempProject& project) {
  auto context = ProjectContext::open(project.dir, ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(context.has_value());
  return std::move(*context);
}

CommandRegistry make_registry() {
  CommandRegistry registry;
  register_property_commands(registry);
  register_prefab_commands(registry);
  return registry;
}

Json run(const CommandRegistry& registry, ProjectContext& context, const char* command, Json args,
         ExecuteOptions options = {.persist = true}, CommandResult* out = nullptr) {
  auto result = registry.execute(context, {command, std::move(args)}, options);
  if (!result) std::fprintf(stderr, "%s failed: %s\n", command, result.error().message.c_str());
  ARCANE_ASSERT_TRUE(result.has_value());
  if (out) *out = *result;
  return result->value;
}

std::string run_error(const CommandRegistry& registry, ProjectContext& context, const char* command, Json args) {
  auto result = registry.execute(context, {command, std::move(args)}, {.persist = true});
  ARCANE_ASSERT_TRUE(!result.has_value());
  return result.error().code;
}

void collect_ids(const arcane::Node& node, std::set<std::uint64_t>& ids, std::size_t& count) {
  ids.insert(node.id());
  ++count;
  for (const auto& c : node.components()) {
    ids.insert(c->id());
    ++count;
  }
  for (const auto& child : node.children()) collect_ids(*child, ids, count);
}

void test_save_instantiate_override_roundtrip() {
  TempProject project;
  CommandRegistry registry = make_registry();
  arcane::NodeId first = 0, second = 0;
  {
    auto context = open(project);
    // The Camera2D target points at the child: it must follow the copy.
    run(registry, *context, "prop.set",
        {{"node", "id:1"}, {"component", "Camera2D"}, {"name", "target"}, {"value", project.child_id}});
    Json saved = run(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", kAsset}});
    ARCANE_ASSERT_TRUE(saved["nodes"] == 2);
    ARCANE_ASSERT_TRUE(run_error(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", kAsset}}) == "CONFLICT");
    ARCANE_ASSERT_TRUE(run_error(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", "../x.prefab.json"}}) == "ARGS_INVALID");
    ARCANE_ASSERT_TRUE(run_error(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", "a/b.json"}}) == "ARGS_INVALID");
    run(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", kAsset}, {"overwrite", true}});

    Json a = run(registry, *context, "prefab.instantiate",
                 {{"asset", kAsset}, {"name", "Rig A"}, {"position", Json::array({10, 20})}});
    Json b = run(registry, *context, "prefab.instantiate",
                 {{"asset", kAsset},
                  {"overrides", Json::array({{{"local_id", 1}, {"component", "Spinner"}, {"property", "label"}, {"value", "b"}}})}});
    first = a["instance_id"].get<arcane::NodeId>();
    second = b["instance_id"].get<arcane::NodeId>();
    ARCANE_ASSERT_TRUE(first != second && a["nodes"].size() == 2);

    // IDs stay unique across the scene and the copies are linked.
    std::set<std::uint64_t> ids;
    std::size_t count = 0;
    for (const auto& root : context->scene().roots()) collect_ids(*root, ids, count);
    ARCANE_ASSERT_TRUE(ids.size() == count && count == 18);

    // Reference remapping: the copy's camera targets the copy's Hand.
    const arcane::NodeId hand_a = a["nodes"][1]["id"].get<arcane::NodeId>();
    Json target = run(registry, *context, "prop.get", {{"node", "id:" + std::to_string(first)}, {"component", "Camera2D"}, {"name", "target"}});
    ARCANE_ASSERT_TRUE(target["value"] == hand_a);

    Json info = run(registry, *context, "prefab.info", {{"node", "id:" + std::to_string(hand_a)}});
    ARCANE_ASSERT_TRUE(info["linked"] == true && info["instance_id"] == first && info["local_id"] == 5);  // ids share one counter: Rig 1, 3 components, Hand 5
    ARCANE_ASSERT_TRUE(info["overrides"].size() == 2);  // name + position

    // Override on a child, then drop it with the same value as the prefab.
    CommandResult over;
    run(registry, *context, "prefab.override",
        {{"node", "id:" + std::to_string(hand_a)}, {"component", "Sprite"}, {"property", "width"}, {"value", 99.5}},
        {.persist = true}, &over);
    ARCANE_ASSERT_TRUE(over.value["overridden"] == true && over.inverse && over.inverse->command == "prefab.revert");
    ARCANE_ASSERT_TRUE(run_error(registry, *context, "prefab.override",
                                 {{"node", "id:" + std::to_string(hand_a)}, {"component", "Sprite"}, {"property", "nope"}, {"value", 1}}) == "PROPERTY_UNKNOWN");
    ARCANE_ASSERT_TRUE(run_error(registry, *context, "prefab.override",
                                 {{"node", "id:" + std::to_string(hand_a)}, {"component", "Sprite"}, {"property", "width"}, {"value", "wide"}}) == "VALUE_INVALID");
    ARCANE_ASSERT_TRUE(run_error(registry, *context, "prefab.override", {{"node", "id:1"}, {"property", "name"}, {"value", "x"}}) == "NODE_NOT_FOUND");
  }
  {
    // Reopened: links, overrides and the remapped reference survived the file.
    auto context = open(project);
    Json info = run(registry, *context, "prefab.info", {{"node", "id:" + std::to_string(first)}});
    ARCANE_ASSERT_TRUE(info["overrides"].size() == 3);
    Json label = run(registry, *context, "prop.get", {{"node", "id:" + std::to_string(second)}, {"component", "Spinner"}, {"name", "label"}});
    ARCANE_ASSERT_TRUE(label["value"] == "b");

    // Revert and its inverse.
    const std::string hand = "id:" + std::to_string(first + 4);  // Rig A: node, 3 components, then Hand
    CommandResult reverted;
    run(registry, *context, "prefab.revert", {{"node", hand}, {"component", "Sprite"}, {"property", "width"}}, {.persist = true}, &reverted);
    ARCANE_ASSERT_TRUE(reverted.value["reverted"] == true && reverted.inverse);
    Json visible = run(registry, *context, "prop.get", {{"node", hand}, {"component", "Sprite"}, {"name", "width"}});
    ARCANE_ASSERT_TRUE(visible["value"] != 99.5);
    ARCANE_ASSERT_TRUE(registry.execute(*context, *reverted.inverse, {.persist = true}).has_value());
    visible = run(registry, *context, "prop.get", {{"node", hand}, {"component", "Sprite"}, {"name", "width"}});
    ARCANE_ASSERT_TRUE(visible["value"] == 99.5);
  }
  // Links are stored in the scene file as explicit data.
  ARCANE_ASSERT_TRUE(slurp(project.dir / "scenes" / "main.scene.json").find(kAsset) != std::string::npos);
}

void test_break_link_and_relink() {
  TempProject project;
  CommandRegistry registry = make_registry();
  auto context = open(project);
  run(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", kAsset}});
  Json a = run(registry, *context, "prefab.instantiate", {{"asset", kAsset}, {"name", "Solo"}});
  const std::string root = "id:" + a["instance_id"].dump();

  CommandResult broke;
  run(registry, *context, "prefab.break_link", {{"node", root}}, {.persist = true}, &broke);
  ARCANE_ASSERT_TRUE(broke.value["nodes"] == 2 && broke.inverse->command == "prefab.relink");
  ARCANE_ASSERT_TRUE(run(registry, *context, "prefab.info", {{"node", root}})["linked"] == false);
  ARCANE_ASSERT_TRUE(run_error(registry, *context, "prefab.break_link", {{"node", root}}) == "NODE_NOT_FOUND");
  // Values stayed.
  ARCANE_ASSERT_TRUE(context->scene().find_node(a["instance_id"].get<arcane::NodeId>())->name() == "Solo");

  ARCANE_ASSERT_TRUE(registry.execute(*context, *broke.inverse, {.persist = true}).has_value());
  Json info = run(registry, *context, "prefab.info", {{"node", root}});
  ARCANE_ASSERT_TRUE(info["linked"] == true && info["overrides"].size() == 1);

  // Undo of instantiate removes the instance; undo of that recreates it with the same overrides.
  CommandResult removed;
  run(registry, *context, "prefab.remove", {{"node", root}}, {.persist = true}, &removed);
  ARCANE_ASSERT_TRUE(context->scene().find_node(a["instance_id"].get<arcane::NodeId>()) == nullptr);
  ARCANE_ASSERT_TRUE(registry.execute(*context, *removed.inverse, {.persist = true}).has_value());

  // dry_run leaves nothing behind.
  const std::string disk = slurp(project.dir / "scenes" / "main.scene.json");
  run(registry, *context, "prefab.instantiate", {{"asset", kAsset}}, {.dry_run = true});
  ARCANE_ASSERT_TRUE(slurp(project.dir / "scenes" / "main.scene.json") == disk);
}

void test_runtime_spawn() {
  TempProject project;
  CommandRegistry registry = make_registry();
  auto context = open(project);
  run(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", kAsset}});

  arcane::Scene scene;
  auto prefab = arcane::load_prefab_file(project.dir, kAsset, context->registry());
  ARCANE_ASSERT_TRUE(prefab.has_value());
  std::set<std::uint64_t> ids;
  std::size_t count = 0;
  for (int i = 0; i < 25; ++i) {
    auto node = arcane::instantiate_prefab(scene, *prefab, kAsset, nullptr, context->registry());
    ARCANE_ASSERT_TRUE(node.has_value());
  }
  auto spawned = arcane::spawn_prefab(scene, project.dir, kAsset, scene.roots().front().get(), context->registry());
  ARCANE_ASSERT_TRUE(spawned.has_value());
  for (const auto& root : scene.roots()) collect_ids(*root, ids, count);
  ARCANE_ASSERT_TRUE(scene.roots().size() == 25 && ids.size() == count && count == 25 * 6 + 6);
  ARCANE_ASSERT_TRUE(!arcane::spawn_prefab(scene, project.dir, "prefabs/none.prefab.json", nullptr, context->registry()).has_value());

  // The scene (with links) round-trips.
  auto text = scene.save_canonical_json(context->registry());
  ARCANE_ASSERT_TRUE(text.has_value());
  auto again = arcane::Scene::load_from_json(*text, context->registry());
  ARCANE_ASSERT_TRUE(again.has_value());
  auto text2 = again->save_canonical_json(context->registry());
  ARCANE_ASSERT_TRUE(text2.has_value() && *text2 == *text);
}

void test_bad_links_refused_and_old_scenes_load() {
  TempProject project;
  CommandRegistry registry = make_registry();
  auto context = open(project);  // a scene without any prefab data loads strictly
  run(registry, *context, "prefab.save", {{"node", "id:1"}, {"asset", kAsset}});
  run(registry, *context, "prefab.instantiate", {{"asset", kAsset}});
  auto text = context->scene().save_canonical_json(context->registry());
  ARCANE_ASSERT_TRUE(text.has_value());
  ARCANE_ASSERT_TRUE(arcane::Scene::load_from_json(*text, context->registry()).has_value());

  auto broken = [&](const std::string& from, const std::string& to) {
    std::string t = *text;
    const auto at = t.find(from);
    ARCANE_ASSERT_TRUE(at != std::string::npos);
    t.replace(at, from.size(), to);
    return arcane::Scene::load_from_json(t, context->registry());
  };
  ARCANE_ASSERT_TRUE(!broken("\"instance_id\": 7", "\"instance_id\": 999").has_value());
  ARCANE_ASSERT_TRUE(!broken("prefabs/rig.prefab.json", "../rig.prefab.json").has_value());
  ARCANE_ASSERT_TRUE(!broken("\"local_id\": 1\n", "\"local_id\": 1, \"extra\": 1\n").has_value());
}

}  // namespace

int main() {
  test_save_instantiate_override_roundtrip();
  test_break_link_and_relink();
  test_runtime_spawn();
  test_bad_links_refused_and_old_scenes_load();
  std::puts("test_prefab: ok");
  return 0;
}
