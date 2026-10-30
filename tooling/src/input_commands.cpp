// input.* commands: the input action map stored in project.json ("input_actions"). They edit
// ProjectFile::input_actions in memory (session().mutable_project()) and mark the project dirty;
// ProjectContext::save writes project.json. Actions are addressed by name, bindings by 0-based index.
// Device names are the persisted ones (keyboard, mouse_button, gamepad_button, gamepad_axis). Action order
// is the persisted order: InputActionMap can only append, so inserting at a position rebuilds the map.
#include <charconv>
#include <cmath>
#include <limits>

#include "arcane/input.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

ToolError fail(const char* code, std::string message, std::string path = {}, std::string hint = {}) {
  return ToolError{code, std::move(message), "project.json", std::move(path), std::move(hint)};
}

constexpr const char* kDeviceNames[] = {"keyboard", "mouse_button", "gamepad_button", "gamepad_axis"};

Json float_json(float v) {
  char buf[32];
  auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
  return std::stod(std::string(buf, end));
}

Json binding_json(const InputBinding& b) {
  return {{"device", kDeviceNames[static_cast<int>(b.device)]}, {"code", b.code},
          {"pad", b.pad}, {"axis_sign", b.axis_sign},
          {"deadzone", float_json(b.deadzone)}, {"threshold", float_json(b.threshold)}};
}

Json action_json(const InputAction& a) {
  Json bindings = Json::array();
  for (const InputBinding& b : a.bindings) bindings.push_back(binding_json(b));
  return {{"name", a.name}, {"player", a.player}, {"bindings", bindings}};
}

std::expected<int, ToolError> parse_int(const Json& v, const std::string& path, int lo, int hi) {
  if (!v.is_number_integer()) return std::unexpected(fail("VALUE_INVALID", "expected an integer", path));
  const long long n = v.get<long long>();
  if (n < lo || n > hi) {
    return std::unexpected(fail("VALUE_INVALID", "must be within [" + std::to_string(lo) + ", " + std::to_string(hi) + "]", path));
  }
  return static_cast<int>(n);
}

std::expected<float, ToolError> parse_float(const Json& v, const std::string& path, float lo, float hi) {
  if (!v.is_number()) return std::unexpected(fail("VALUE_INVALID", "expected a number", path));
  const double d = v.get<double>();
  if (!(d >= lo && d <= hi)) {
    return std::unexpected(fail("VALUE_INVALID", "must be within [" + std::to_string(lo) + ", " + std::to_string(hi) + "]", path));
  }
  return static_cast<float>(d);
}

std::expected<InputDevice, ToolError> parse_device(const Json& v, const std::string& path) {
  if (v.is_string()) {
    for (int i = 0; i < 4; ++i) {
      if (v.get<std::string>() == kDeviceNames[i]) return static_cast<InputDevice>(i);
    }
  }
  return std::unexpected(fail("VALUE_INVALID", "unknown input device", path,
                              "Use keyboard, mouse_button, gamepad_button or gamepad_axis"));
}

std::expected<void, ToolError> set_field(InputBinding& b, const std::string& field, const Json& v, const std::string& path) {
  if (field == "device") {
    auto d = parse_device(v, path);
    if (!d) return std::unexpected(d.error());
    b.device = *d;
  } else if (field == "code") {
    auto n = parse_int(v, path, 0, std::numeric_limits<int>::max());
    if (!n) return std::unexpected(n.error());
    b.code = *n;
  } else if (field == "pad") {
    auto n = parse_int(v, path, -1, kMaxGamepads - 1);
    if (!n) return std::unexpected(n.error());
    b.pad = *n;
  } else if (field == "axis_sign") {
    auto n = parse_int(v, path, -1, 1);
    if (!n) return std::unexpected(n.error());
    b.axis_sign = *n;
  } else if (field == "deadzone") {
    auto f = parse_float(v, path, 0.0f, 0.99f);
    if (!f) return std::unexpected(f.error());
    b.deadzone = *f;
  } else if (field == "threshold") {
    auto f = parse_float(v, path, 0.0f, 1.0f);
    if (!f) return std::unexpected(f.error());
    b.threshold = *f;
  } else {
    return std::unexpected(fail("ARGS_INVALID", "'" + field + "' is not a binding field", "/field",
                                "Valid fields: device, code, pad, axis_sign, deadzone, threshold"));
  }
  return {};
}

std::expected<InputBinding, ToolError> parse_binding(const Json& obj, const std::string& base) {
  InputBinding b;
  if (!obj.is_object()) return std::unexpected(fail("VALUE_INVALID", "expected a binding object", base));
  for (const char* key : {"device", "code", "pad", "axis_sign", "deadzone", "threshold"}) {
    if (!obj.contains(key)) continue;
    if (auto ok = set_field(b, key, obj[key], base + "/" + key); !ok) return std::unexpected(ok.error());
  }
  for (const auto& [key, value] : obj.items()) {
    (void)value;
    if (key != "device" && key != "code" && key != "pad" && key != "axis_sign" && key != "deadzone" && key != "threshold") {
      return std::unexpected(fail("ARGS_INVALID", "'" + key + "' is not a binding field", base + "/" + key));
    }
  }
  return b;
}

std::expected<int, ToolError> parse_player(const Json& v, const std::string& path) { return parse_int(v, path, -1, kMaxGamepads - 1); }

// Rebuilds the map with `actions` in order (set_action only appends or replaces in place).
void rebuild(InputActionMap& map, std::vector<InputAction> actions) {
  map = InputActionMap{};
  for (InputAction& a : actions) map.set_action(std::move(a));
}

InputAction* find_action(InputActionMap& map, const std::string& name) {
  for (const InputAction& a : map.actions()) {
    if (a.name == name) return const_cast<InputAction*>(&a);
  }
  return nullptr;
}

std::expected<const InputAction*, ToolError> require_action(InputActionMap& map, const Json& args) {
  const InputAction* a = find_action(map, args["action"].get<std::string>());
  if (!a) {
    return std::unexpected(fail("VALUE_INVALID", "no input action '" + args["action"].get<std::string>() + "'", "/action",
                                "List actions with input.list"));
  }
  return a;
}

std::expected<std::size_t, ToolError> check_binding_index(const InputAction& a, const Json& args, bool allow_end = false) {
  if (!args["index"].is_number_integer()) return std::unexpected(fail("VALUE_INVALID", "expected an integer", "/index"));
  const long long i = args["index"].get<long long>();
  const long long limit = static_cast<long long>(a.bindings.size()) + (allow_end ? 1 : 0);
  if (i < 0 || i >= limit) {
    return std::unexpected(fail("VALUE_INVALID",
                                "binding index " + std::to_string(i) + " is out of range (" + std::to_string(a.bindings.size()) +
                                    " bindings)",
                                "/index", "List bindings with input.list"));
  }
  return static_cast<std::size_t>(i);
}

Json schema(Json properties, std::initializer_list<const char*> required) {
  Json req = Json::array();
  for (const char* r : required) req.push_back(r);
  return {{"type", "object"}, {"properties", std::move(properties)}, {"required", req}, {"additionalProperties", false}};
}

const Json kStr = {{"type", "string"}};
const Json kInt = {{"type", "integer"}};
const Json kNum = {{"type", "number"}};
const Json kDeviceEnum = {{"type", "string"}, {"enum", Json::array({"keyboard", "mouse_button", "gamepad_button", "gamepad_axis"})}};

Json binding_props(Json extra = Json::object()) {
  Json p = {{"action", kStr}, {"device", kDeviceEnum}, {"code", kInt}, {"pad", kInt},
            {"axis_sign", kInt}, {"deadzone", kNum}, {"threshold", kNum}};
  for (const auto& [k, v] : extra.items()) p[k] = v;
  return p;
}

}  // namespace

void register_input_commands(CommandRegistry& registry) {
  registry.add(
      {"input.list", "List the input actions (name, player, bindings with device/code/pad/axis_sign/deadzone/threshold).",
       schema(Json::object(), {}), Json{{"type", "object"}}, false},
      [](ProjectContext& context, const Json&) -> std::expected<CommandResult, ToolError> {
        Json actions = Json::array();
        for (const InputAction& a : context.session().project().input_actions.actions()) actions.push_back(action_json(a));
        return CommandResult{{{"count", actions.size()}, {"actions", actions}}, std::nullopt, std::nullopt};
      });

  registry.add(
      {"input.add_action", "Add an empty input action `name` for `player` (-1 = any pad, default).",
       schema({{"name", kStr}, {"player", kInt}}, {"name"}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        InputActionMap& map = context.session().mutable_project().input_actions;
        const std::string name = args["name"].get<std::string>();
        if (name.empty()) return std::unexpected(fail("VALUE_INVALID", "action name must not be empty", "/name"));
        if (find_action(map, name)) {
          return std::unexpected(fail("VALUE_INVALID", "input action '" + name + "' already exists", "/name",
                                      "List actions with input.list"));
        }
        int player = -1;
        if (args.contains("player")) {
          auto p = parse_player(args["player"], "/player");
          if (!p) return std::unexpected(p.error());
          player = *p;
        }
        map.set_action(InputAction{.name = name, .bindings = {}, .player = player});
        context.mark_project_dirty();
        return CommandResult{{{"name", name}, {"player", player}}, Invocation{"input.remove_action", {{"action", name}}},
                             std::nullopt};
      });

  registry.add(
      {"input.remove_action", "Remove the input action `action` with its bindings.",
       schema({{"action", kStr}}, {"action"}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        InputActionMap& map = context.session().mutable_project().input_actions;
        auto action = require_action(map, args);
        if (!action) return std::unexpected(action.error());
        const Json restore = {{"action", action_json(**action)}, {"index", *action - map.actions().data()}};
        const std::string name = (*action)->name;
        map.remove_action(name);
        context.mark_project_dirty();
        return CommandResult{{{"name", name}}, Invocation{"input.restore_action", restore}, std::nullopt};
      });

  registry.add(
      {"input.restore_action",
       "Recreate an input action exactly from an input.list entry `action` {name, player, bindings}, inserted at `index` "
       "(default append). Inverse of input.remove_action.",
       schema({{"action", {{"type", "object"}}}, {"index", kInt}}, {"action"}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        InputActionMap& map = context.session().mutable_project().input_actions;
        const Json& obj = args["action"];
        if (!obj.is_object() || !obj.contains("name") || !obj["name"].is_string() || obj["name"].get<std::string>().empty()) {
          return std::unexpected(fail("VALUE_INVALID", "action needs a non-empty string name", "/action/name"));
        }
        InputAction action;
        action.name = obj["name"].get<std::string>();
        if (find_action(map, action.name)) {
          return std::unexpected(fail("VALUE_INVALID", "input action '" + action.name + "' already exists", "/action/name",
                                      "List actions with input.list"));
        }
        if (obj.contains("player")) {
          auto p = parse_player(obj["player"], "/action/player");
          if (!p) return std::unexpected(p.error());
          action.player = *p;
        }
        if (obj.contains("bindings")) {
          if (!obj["bindings"].is_array()) return std::unexpected(fail("VALUE_INVALID", "expected an array", "/action/bindings"));
          for (std::size_t i = 0; i < obj["bindings"].size(); ++i) {
            auto b = parse_binding(obj["bindings"][i], "/action/bindings/" + std::to_string(i));
            if (!b) return std::unexpected(b.error());
            action.bindings.push_back(*b);
          }
        }
        std::vector<InputAction> actions = map.actions();
        long long index = static_cast<long long>(actions.size());
        if (args.contains("index")) {
          if (!args["index"].is_number_integer()) return std::unexpected(fail("VALUE_INVALID", "expected an integer", "/index"));
          index = args["index"].get<long long>();
          if (index < 0 || index > static_cast<long long>(actions.size())) {
            return std::unexpected(fail("VALUE_INVALID", "index " + std::to_string(index) + " is out of range", "/index"));
          }
        }
        const std::string name = action.name;
        actions.insert(actions.begin() + index, std::move(action));
        rebuild(map, std::move(actions));
        context.mark_project_dirty();
        return CommandResult{{{"name", name}, {"index", index}}, Invocation{"input.remove_action", {{"action", name}}},
                             std::nullopt};
      });

  registry.add(
      {"input.set_player", "Set the player (gamepad number, -1 = any pad) of an action.",
       schema({{"action", kStr}, {"player", kInt}}, {"action", "player"}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        InputActionMap& map = context.session().mutable_project().input_actions;
        auto action = require_action(map, args);
        if (!action) return std::unexpected(action.error());
        auto player = parse_player(args["player"], "/player");
        if (!player) return std::unexpected(player.error());
        const int previous = (*action)->player;
        const Json value = {{"action", args["action"]}, {"previous", previous}, {"player", *player}, {"changed", previous != *player}};
        if (previous == *player) return CommandResult{value, std::nullopt, std::nullopt};
        map.set_player((*action)->name, *player);
        context.mark_project_dirty();
        return CommandResult{value, Invocation{"input.set_player", {{"action", args["action"]}, {"player", previous}}}, std::nullopt};
      });

  registry.add(
      {"input.add_binding",
       "Add a binding to an action (device keyboard|mouse_button|gamepad_button|gamepad_axis, code, pad -1..3, axis_sign "
       "-1..1, deadzone 0..0.99, threshold 0..1; axis fields only persist for gamepad_axis). `index` inserts (default append).",
       schema(binding_props({{"index", kInt}}), {"action"}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        InputActionMap& map = context.session().mutable_project().input_actions;
        auto action = require_action(map, args);
        if (!action) return std::unexpected(action.error());
        Json fields = args;
        fields.erase("action");
        fields.erase("index");
        auto binding = parse_binding(fields, "");
        if (!binding) return std::unexpected(binding.error());
        std::size_t index = (*action)->bindings.size();
        if (args.contains("index")) {
          auto i = check_binding_index(**action, args, true);
          if (!i) return std::unexpected(i.error());
          index = *i;
        }
        InputAction updated = **action;
        updated.bindings.insert(updated.bindings.begin() + static_cast<std::ptrdiff_t>(index), *binding);
        map.set_action(std::move(updated));
        context.mark_project_dirty();
        return CommandResult{{{"index", index}},
                             Invocation{"input.remove_binding", {{"action", args["action"]}, {"index", index}}}, std::nullopt};
      });

  registry.add(
      {"input.remove_binding", "Remove the binding at `index` of an action.",
       schema({{"action", kStr}, {"index", kInt}}, {"action", "index"}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        InputActionMap& map = context.session().mutable_project().input_actions;
        auto action = require_action(map, args);
        if (!action) return std::unexpected(action.error());
        auto index = check_binding_index(**action, args);
        if (!index) return std::unexpected(index.error());
        Json restore = binding_json((*action)->bindings[*index]);
        restore["action"] = args["action"];
        restore["index"] = *index;
        InputAction updated = **action;
        updated.bindings.erase(updated.bindings.begin() + static_cast<std::ptrdiff_t>(*index));
        map.set_action(std::move(updated));
        context.mark_project_dirty();
        return CommandResult{{{"index", *index}}, Invocation{"input.add_binding", std::move(restore)}, std::nullopt};
      });

  registry.add(
      {"input.set_binding",
       "Set one field (device, code, pad, axis_sign, deadzone, threshold) of the binding at `index` of an action.",
       schema({{"action", kStr}, {"index", kInt}, {"field", kStr}, {"value", Json::object()}},
              {"action", "index", "field", "value"}),
       Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        InputActionMap& map = context.session().mutable_project().input_actions;
        auto action = require_action(map, args);
        if (!action) return std::unexpected(action.error());
        auto index = check_binding_index(**action, args);
        if (!index) return std::unexpected(index.error());
        const std::string field = args["field"].get<std::string>();
        const InputBinding& current = (*action)->bindings[*index];
        InputBinding trial = current;
        if (auto ok = set_field(trial, field, args["value"], "/value"); !ok) return std::unexpected(ok.error());
        const Json previous = binding_json(current)[field];
        const Json now = binding_json(trial)[field];
        const Json value = {{"action", args["action"]}, {"index", *index}, {"field", field},
                            {"previous", previous}, {"value", now}, {"changed", trial != current}};
        if (trial == current) return CommandResult{value, std::nullopt, std::nullopt};
        map.replace_binding((*action)->name, *index, trial);
        context.mark_project_dirty();
        return CommandResult{value,
                             Invocation{"input.set_binding",
                                        {{"action", args["action"]}, {"index", *index}, {"field", field}, {"value", previous}}},
                             std::nullopt};
      });
}

}  // namespace arcane::tooling
