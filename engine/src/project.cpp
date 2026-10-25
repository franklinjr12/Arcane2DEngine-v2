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
  return project;
}

}  // namespace arcane
