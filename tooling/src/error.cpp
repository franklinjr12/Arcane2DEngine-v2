#include "arcane_tooling/error.hpp"

#include <unordered_map>

namespace arcane::tooling {

int exit_code(const ToolError& error) {
  static const std::unordered_map<std::string, int> kCodes = {
      {"USAGE", 2},           {"UNKNOWN_COMMAND", 2},   {"ARGS_INVALID", 2},
      {"PROJECT_NOT_FOUND", 3}, {"IO_ERROR", 3},
      {"SCHEMA_INVALID", 4},  {"VALUE_INVALID", 4},     {"REPLAY_DIVERGED", 4},     {"PROPERTY_UNKNOWN", 4},
      {"NODE_NOT_FOUND", 4},  {"COMPONENT_NOT_FOUND", 4}, {"COMPONENT_TYPE_UNKNOWN", 4},
      {"SELECTOR_AMBIGUOUS", 5}, {"CONFLICT", 5},
      {"MODULE_LOAD_FAILED", 6}, {"MODULE_FAILURE", 6},
      {"UNSUPPORTED_VERSION", 7},
  };
  auto it = kCodes.find(error.code);
  return it == kCodes.end() ? 1 : it->second;
}

Json error_json(const ToolError& error) {
  auto field = [](const std::string& s) { return s.empty() ? Json(nullptr) : Json(s); };
  Json body = {{"code", error.code},
               {"message", error.message},
               {"file", field(error.file)},
               {"path", field(error.path)},
               {"hint", field(error.hint)}};
  return Json{{"ok", false}, {"error", body}};
}

}  // namespace arcane::tooling
