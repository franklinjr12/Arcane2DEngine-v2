// animgraph.* commands (docs/commands.md): create and edit animation state machine files (`arcane.animgraph`, see
// docs/formats/animgraph.md). Like clip.* they read and write the graph asset file only and write it at once (no
// persist/dry_run); every edit returns animgraph.restore with the previous file bytes as its inverse.
#include <filesystem>
#include <functional>
#include <optional>

#include "arcane/animgraph.hpp"
#include "arcane/assets.hpp"
#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

namespace fs = std::filesystem;

ToolError fail(const char* code, std::string message, std::string path = {}, std::string hint = {}, std::string file = {}) {
  return ToolError{code, std::move(message), std::move(file), std::move(path), std::move(hint)};
}

Json schema(Json properties, std::initializer_list<const char*> required) {
  Json req = Json::array();
  for (const char* r : required) req.push_back(r);
  return {{"type", "object"}, {"properties", std::move(properties)}, {"required", req}, {"additionalProperties", false}};
}

const Json kString = {{"type", "string"}};
const Json kBool = {{"type", "boolean"}};
const Json kIndex = {{"type", "integer"}, {"minimum", 0}};
const Json kInt = {{"type", "integer"}};
const Json kNumber = {{"type", "number"}};
const Json kConditions = {{"type", "array"},
                          {"items", {{"type", "object"},
                                     {"properties", {{"param", kString}, {"op", kString}, {"value", kNumber}}},
                                     {"required", Json::array({"param", "op"})},
                                     {"additionalProperties", false}}}};

std::expected<void, ToolError> check_path(const std::string& value, const char* arg) {
  if (is_project_relative_path(value)) return {};
  return std::unexpected(fail("ARGS_INVALID", "'" + value + "' is not a project-relative '/' path", std::string("/") + arg));
}

std::optional<std::string> read_file(ProjectContext& context, const std::string& path) {
  std::error_code ec;
  if (!fs::exists(context.project_dir() / path, ec)) return std::nullopt;
  auto text = read_text_file(context.project_dir() / path);
  if (!text) return std::nullopt;
  return std::move(*text);
}

Invocation restore_call(const std::string& path, const std::optional<std::string>& previous) {
  Json args = {{"graph", path}};
  if (previous) args["content"] = *previous;
  else args["absent"] = true;
  return Invocation{"animgraph.restore", std::move(args)};
}

std::expected<void, ToolError> write_graph(ProjectContext& context, const std::string& path, const AnimGraph& graph) {
  if (const std::string problem = validate_animgraph(graph); !problem.empty()) {
    return std::unexpected(fail("VALUE_INVALID", problem, "", {}, path));
  }
  auto text = save_animgraph_canonical_json(graph);
  if (!text) return std::unexpected(fail("VALUE_INVALID", text.error(), "", {}, path));
  const auto target = context.project_dir() / path;
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  if (auto ok = write_text_file_atomic(target, *text); !ok) return std::unexpected(fail("IO_ERROR", ok.error(), "", {}, path));
  return {};
}

std::expected<AnimGraph, ToolError> load_graph(ProjectContext& context, const std::string& path) {
  if (auto ok = check_path(path, "graph"); !ok) return std::unexpected(ok.error());
  const auto text = read_file(context, path);
  if (!text) {
    return std::unexpected(fail("IO_ERROR", "graph " + path + " does not exist", "/graph", "Create it with animgraph.create", path));
  }
  auto graph = load_animgraph_from_json(*text);
  if (!graph) return std::unexpected(fail("SCHEMA_INVALID", graph.error(), "/graph", {}, path));
  return std::move(*graph);
}

using Edit = std::function<std::expected<Json, ToolError>(AnimGraph&)>;

// Loads `args["graph"]`, applies `edit`, and writes the result unless it equals the file as loaded.
std::expected<CommandResult, ToolError> modify(ProjectContext& context, const Json& args, const Edit& edit) {
  const std::string path = args["graph"];
  auto graph = load_graph(context, path);
  if (!graph) return std::unexpected(graph.error());
  const std::string before = save_animgraph_canonical_json(*graph).value_or("");
  auto extra = edit(*graph);
  if (!extra) return std::unexpected(extra.error());
  Json value = {{"graph", path}};
  for (auto& [key, v] : extra->items()) value[key] = v;
  if (const std::string problem = validate_animgraph(*graph); !problem.empty()) {
    return std::unexpected(fail("VALUE_INVALID", problem, "", {}, path));
  }
  if (save_animgraph_canonical_json(*graph).value_or("") == before) {
    value["changed"] = false;
    return CommandResult{std::move(value), std::nullopt};
  }
  const auto previous = read_file(context, path);
  if (auto ok = write_graph(context, path, *graph); !ok) return std::unexpected(ok.error());
  value["changed"] = true;
  return CommandResult{std::move(value), restore_call(path, previous)};
}

std::expected<AssetId, ToolError> clip_id(ProjectContext& context, const std::string& clip) {
  if (auto ok = check_path(clip, "clip"); !ok) return std::unexpected(ok.error());
  constexpr const char* kAssets = "assets/";
  if (clip.rfind(kAssets, 0) != 0) {
    return std::unexpected(fail("ARGS_INVALID", "clip must be a file under assets/ (e.g. assets/hero_idle.json)", "/clip"));
  }
  std::error_code ec;
  if (!fs::exists(context.project_dir() / clip, ec)) {
    return std::unexpected(fail("IO_ERROR", "clip " + clip + " does not exist", "/clip", "Create it with clip.create", clip));
  }
  return asset_id_for_relative_path(clip.substr(7));
}

std::expected<AnimParamType, ToolError> parse_type(const std::string& type) {
  if (type == "bool") return AnimParamType::Bool;
  if (type == "float") return AnimParamType::Float;
  if (type == "trigger") return AnimParamType::Trigger;
  return std::unexpected(fail("ARGS_INVALID", "type must be bool, float or trigger", "/type"));
}

Json condition_json(const AnimCondition& c) {
  Json j = {{"param", c.param}, {"op", anim_compare_name(c.op)}};
  if (c.op != AnimCompare::IsTrue && c.op != AnimCompare::IsFalse && c.op != AnimCompare::Fired) j["value"] = c.value;
  return j;
}

Json graph_json(const AnimGraph& g) {
  Json params = Json::array(), states = Json::array(), transitions = Json::array();
  for (const AnimParam& p : g.parameters) {
    Json e = {{"name", p.name}, {"type", p.type == AnimParamType::Bool ? "bool" : p.type == AnimParamType::Float ? "float" : "trigger"}};
    if (p.type != AnimParamType::Trigger) e["default"] = p.default_value;
    params.push_back(std::move(e));
  }
  for (const AnimState& s : g.states) {
    Json events = Json::array();
    for (const AnimEvent& e : s.events) events.push_back({{"frame", e.frame}, {"name", e.name}});
    states.push_back({{"name", s.name}, {"clip", s.clip}, {"speed", s.speed}, {"events", events}});
  }
  for (std::size_t i = 0; i < g.transitions.size(); ++i) {
    const AnimTransition& t = g.transitions[i];
    Json conditions = Json::array();
    for (const AnimCondition& c : t.conditions) conditions.push_back(condition_json(c));
    transitions.push_back({{"index", i}, {"from", t.from}, {"to", t.to}, {"priority", t.priority}, {"conditions", conditions}});
  }
  return {{"initial", g.initial}, {"parameters", params}, {"states", states}, {"transitions", transitions}};
}

AnimState* state_named(AnimGraph& g, const std::string& name) {
  for (AnimState& s : g.states) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

ToolError no_state(const std::string& name, const char* where) {
  return fail("VALUE_INVALID", "state '" + name + "' does not exist", where, "See animgraph.info");
}

std::expected<AnimCondition, ToolError> parse_condition(const Json& j, const std::string& where) {
  AnimCondition c;
  c.param = j.value("param", std::string());
  const std::string op = j.value("op", std::string());
  bool found = false;
  for (AnimCompare candidate : {AnimCompare::IsTrue, AnimCompare::IsFalse, AnimCompare::Less, AnimCompare::LessEqual,
                                AnimCompare::Greater, AnimCompare::GreaterEqual, AnimCompare::Equal, AnimCompare::NotEqual,
                                AnimCompare::Fired}) {
    if (op == anim_compare_name(candidate)) {
      c.op = candidate;
      found = true;
    }
  }
  if (!found) {
    return std::unexpected(fail("ARGS_INVALID", "unknown operator '" + op + "'", where + "/op",
                                "One of is_true is_false lt le gt ge eq ne fired"));
  }
  c.value = j.value("value", 0.0f);
  return c;
}

}  // namespace

void register_animgraph_commands(CommandRegistry& registry) {
  registry.add(
      {"animgraph.create",
       "Create an empty animation graph file (`arcane.animgraph` v2) at a project-relative path such as assets/hero.animgraph.json. "
       "Fails if the file exists unless overwrite=true. Writes the file at once; the inverse is animgraph.restore.",
       schema({{"graph", kString}, {"overwrite", kBool}}, {"graph"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string path = args["graph"];
        if (auto ok = check_path(path, "graph"); !ok) return std::unexpected(ok.error());
        const auto previous = read_file(context, path);
        std::error_code ec;
        if (fs::exists(context.project_dir() / path, ec) && !args.value("overwrite", false)) {
          return std::unexpected(fail("CONFLICT", path + " already exists", "/graph", "Pass overwrite=true to replace it", path));
        }
        if (auto ok = write_graph(context, path, AnimGraph{}); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"graph", path}}, restore_call(path, previous)};
      });

  registry.add(
      {"animgraph.info",
       "Describe an animation graph file: initial state, parameters, states (clip asset id, speed, events) and transitions "
       "(index, from, to, priority, conditions).",
       schema({{"graph", kString}}, {"graph"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto graph = load_graph(context, args["graph"]);
        if (!graph) return std::unexpected(graph.error());
        Json value = graph_json(*graph);
        value["graph"] = args["graph"];
        return CommandResult{std::move(value), std::nullopt};
      });

  registry.add(
      {"animgraph.add_param",
       "Add a parameter `name` of `type` bool, float or trigger (`default` for bool/float, default 0; a bool default is 0 or 1).",
       schema({{"graph", kString}, {"name", kString}, {"type", kString}, {"default", kNumber}}, {"graph", "name", "type"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          auto type = parse_type(args["type"]);
          if (!type) return std::unexpected(type.error());
          const std::string name = args["name"];
          if (g.find_param(name)) return std::unexpected(fail("CONFLICT", "parameter '" + name + "' already exists", "/name"));
          g.parameters.push_back({name, *type, *type == AnimParamType::Trigger ? 0.0f : args.value("default", 0.0f)});
          return Json{{"name", name}};
        });
      });

  registry.add(
      {"animgraph.remove_param", "Remove parameter `name`; it must not be used by a transition condition.",
       schema({{"graph", kString}, {"name", kString}}, {"graph", "name"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          const std::string name = args["name"];
          if (!g.find_param(name)) return std::unexpected(fail("VALUE_INVALID", "parameter '" + name + "' does not exist", "/name"));
          for (const AnimTransition& t : g.transitions) {
            for (const AnimCondition& c : t.conditions) {
              if (c.param == name) {
                return std::unexpected(fail("CONFLICT", "parameter '" + name + "' is used by a transition", "/name",
                                            "Remove that transition first (animgraph.remove_transition)"));
              }
            }
          }
          std::erase_if(g.parameters, [&](const AnimParam& p) { return p.name == name; });
          return Json{{"name", name}};
        });
      });

  registry.add(
      {"animgraph.add_state",
       "Add a state `name` playing `clip` (a project-relative file under assets/, e.g. assets/hero_idle.json) at `speed` "
       "(default 1). The first state becomes the initial state.",
       schema({{"graph", kString}, {"name", kString}, {"clip", kString}, {"speed", kNumber}}, {"graph", "name", "clip"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          const std::string name = args["name"];
          if (state_named(g, name)) return std::unexpected(fail("CONFLICT", "state '" + name + "' already exists", "/name"));
          auto id = clip_id(context, args["clip"]);
          if (!id) return std::unexpected(id.error());
          g.states.push_back({name, *id, args.value("speed", 1.0f), {}});
          if (g.initial.empty()) g.initial = name;
          return Json{{"name", name}, {"clip_id", *id}};
        });
      });

  registry.add(
      {"animgraph.set_state", "Change state `name`: `clip`, `speed` and/or `initial`=true (make it the initial state).",
       schema({{"graph", kString}, {"name", kString}, {"clip", kString}, {"speed", kNumber}, {"initial", kBool}}, {"graph", "name"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          if (!args.contains("clip") && !args.contains("speed") && !args.contains("initial")) {
            return std::unexpected(fail("ARGS_INVALID", "give clip, speed and/or initial", "/clip"));
          }
          AnimState* s = state_named(g, args["name"]);
          if (!s) return std::unexpected(no_state(args["name"], "/name"));
          if (args.contains("clip")) {
            auto id = clip_id(context, args["clip"]);
            if (!id) return std::unexpected(id.error());
            s->clip = *id;
          }
          if (args.contains("speed")) s->speed = args["speed"];
          if (args.value("initial", false)) g.initial = s->name;
          return Json{{"name", s->name}};
        });
      });

  registry.add(
      {"animgraph.remove_state",
       "Remove state `name`; it must not be the initial state or an end of a transition.",
       schema({{"graph", kString}, {"name", kString}}, {"graph", "name"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          const std::string name = args["name"];
          if (!state_named(g, name)) return std::unexpected(no_state(name, "/name"));
          if (g.initial == name) {
            return std::unexpected(fail("CONFLICT", "'" + name + "' is the initial state", "/name", "Pick another with animgraph.set_state initial=true"));
          }
          for (const AnimTransition& t : g.transitions) {
            if (t.from == name || t.to == name) {
              return std::unexpected(fail("CONFLICT", "state '" + name + "' is used by a transition", "/name",
                                          "Remove that transition first (animgraph.remove_transition)"));
            }
          }
          std::erase_if(g.states, [&](const AnimState& s) { return s.name == name; });
          return Json{{"name", name}};
        });
      });

  registry.add(
      {"animgraph.add_event",
       "Attach event `name` to clip frame `frame` of state `state`; it is delivered to the node's components "
       "(Component::on_animation_event) when playback enters that frame.",
       schema({{"graph", kString}, {"state", kString}, {"frame", kIndex}, {"name", kString}}, {"graph", "state", "frame", "name"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          AnimState* s = state_named(g, args["state"]);
          if (!s) return std::unexpected(no_state(args["state"], "/state"));
          if (args["name"].get<std::string>().empty()) return std::unexpected(fail("ARGS_INVALID", "name must not be empty", "/name"));
          s->events.push_back({args["frame"], args["name"]});
          return Json{{"index", s->events.size() - 1}};
        });
      });

  registry.add(
      {"animgraph.remove_event", "Remove event number `index` (see animgraph.info) of state `state`.",
       schema({{"graph", kString}, {"state", kString}, {"index", kIndex}}, {"graph", "state", "index"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          AnimState* s = state_named(g, args["state"]);
          if (!s) return std::unexpected(no_state(args["state"], "/state"));
          const std::size_t index = args["index"];
          if (index >= s->events.size()) return std::unexpected(fail("VALUE_INVALID", "event " + std::to_string(index) + " does not exist", "/index"));
          s->events.erase(s->events.begin() + static_cast<std::ptrdiff_t>(index));
          return Json{{"index", index}};
        });
      });

  registry.add(
      {"animgraph.add_transition",
       "Add a transition `from` a state (or \"*\" for any) `to` a state with `priority` (default 0, higher wins) and "
       "`conditions` [{param, op, value}] that must all hold (op: is_true/is_false for bool, lt le gt ge eq ne for float with "
       "`value`, fired for trigger; none = always). Ties go to the earlier transition. Appended, or inserted at `index`.",
       schema({{"graph", kString}, {"from", kString}, {"to", kString}, {"priority", kInt}, {"conditions", kConditions}, {"index", kIndex}},
              {"graph", "from", "to"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          AnimTransition t;
          t.from = args["from"];
          t.to = args["to"];
          t.priority = args.value("priority", 0);
          if (args.contains("conditions")) {
            for (std::size_t i = 0; i < args["conditions"].size(); ++i) {
              auto c = parse_condition(args["conditions"][i], "/conditions/" + std::to_string(i));
              if (!c) return std::unexpected(c.error());
              t.conditions.push_back(std::move(*c));
            }
          }
          const std::size_t index = args.contains("index") ? args["index"].get<std::size_t>() : g.transitions.size();
          if (index > g.transitions.size()) return std::unexpected(fail("VALUE_INVALID", "index is above the transition count", "/index"));
          g.transitions.insert(g.transitions.begin() + static_cast<std::ptrdiff_t>(index), std::move(t));
          return Json{{"index", index}};
        });
      });

  registry.add(
      {"animgraph.remove_transition", "Remove transition number `index` (see animgraph.info).",
       schema({{"graph", kString}, {"index", kIndex}}, {"graph", "index"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) {
        return modify(context, args, [&](AnimGraph& g) -> std::expected<Json, ToolError> {
          const std::size_t index = args["index"];
          if (index >= g.transitions.size()) return std::unexpected(fail("VALUE_INVALID", "transition " + std::to_string(index) + " does not exist", "/index"));
          g.transitions.erase(g.transitions.begin() + static_cast<std::ptrdiff_t>(index));
          return Json{{"index", index}};
        });
      });

  registry.add(
      {"animgraph.restore",
       "Put an animation graph file back to an earlier state: write `content` (the exact previous file text) or, with absent=true, "
       "delete the file. The inverse of the other animgraph.* writes; not meant for authoring.",
       schema({{"graph", kString}, {"content", kString}, {"absent", kBool}}, {"graph"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string path = args["graph"];
        if (auto ok = check_path(path, "graph"); !ok) return std::unexpected(ok.error());
        const bool absent = args.value("absent", false);
        if (absent == args.contains("content")) {
          return std::unexpected(fail("ARGS_INVALID", "give either content or absent=true", "/content"));
        }
        const auto previous = read_file(context, path);
        const auto target = context.project_dir() / path;
        std::error_code ec;
        if (absent) {
          fs::remove(target, ec);
          if (ec) return std::unexpected(fail("IO_ERROR", "cannot remove " + path + ": " + ec.message(), "", {}, path));
        } else {
          fs::create_directories(target.parent_path(), ec);
          if (auto ok = write_text_file_atomic(target, args["content"].get<std::string>()); !ok) {
            return std::unexpected(fail("IO_ERROR", ok.error(), "", {}, path));
          }
        }
        return CommandResult{{{"graph", path}, {"restored", absent ? "absent" : "content"}}, restore_call(path, previous)};
      });
}

}  // namespace arcane::tooling
