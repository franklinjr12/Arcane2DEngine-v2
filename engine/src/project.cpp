#include "arcane/project.hpp"

#include <format>
#include <json.hpp>

namespace arcane {

namespace {
constexpr int kProjectFormatVersion = 1;
}  // namespace

std::string save_project_to_json(const ProjectFile& project) {
  nlohmann::json root;
  root["format_version"] = kProjectFormatVersion;
  root["name"] = project.name;
  root["main_scene"] = project.main_scene;
  root["module"] = project.module;
  root["input_actions"] = nlohmann::json::parse(project.input_actions.save_to_json());
  root["hud"] = nlohmann::json::parse(project.hud.save_to_json());
  return root.dump(2);
}

std::expected<ProjectFile, std::string> load_project_from_json(const std::string& text) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (const nlohmann::json::parse_error& ex) {
    return std::unexpected(std::format("malformed project JSON: {}", ex.what()));
  }

  if (!root.contains("format_version") || !root["format_version"].is_number_integer()) {
    return std::unexpected(std::string("project JSON is missing a numeric 'format_version' field"));
  }
  const int file_version = root["format_version"].get<int>();
  if (file_version != kProjectFormatVersion) {
    return std::unexpected(std::format(
        "unsupported project format_version {} (this build only supports version {}; {} than supported)",
        file_version, kProjectFormatVersion, file_version > kProjectFormatVersion ? "newer" : "older"));
  }
  if (!root.contains("name") || !root.contains("main_scene")) {
    return std::unexpected(std::string("project JSON is missing 'name' or 'main_scene'"));
  }

  ProjectFile project;
  project.name = root["name"].get<std::string>();
  project.main_scene = root["main_scene"].get<std::string>();
  project.module = root.value("module", std::string());
  if (root.contains("input_actions")) {
    auto input_actions = InputActionMap::load_from_json(root["input_actions"].dump());
    if (!input_actions) {
      return std::unexpected(input_actions.error());
    }
    project.input_actions = std::move(*input_actions);
  }
  if (root.contains("hud")) {
    auto hud = Hud::load_from_json(root["hud"].dump());
    if (!hud) {
      return std::unexpected(hud.error());
    }
    project.hud = std::move(*hud);
  }
  return project;
}

}  // namespace arcane
