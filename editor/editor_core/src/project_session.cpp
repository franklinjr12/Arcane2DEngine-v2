#include "arcane_editor/project_session.hpp"

#include <format>
#include <fstream>
#include <sstream>

namespace arcane::editor {

namespace {

std::expected<std::string, std::string> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected(std::format("failed to open '{}' for reading", path.string()));
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::expected<void, std::string> write_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return std::unexpected(std::format("failed to open '{}' for writing", path.string()));
  }
  out << text;
  out.close();
  if (!out) {
    return std::unexpected(std::format("failed writing '{}'", path.string()));
  }
  return {};
}

}  // namespace

std::expected<ProjectSession, std::string> ProjectSession::open(const std::filesystem::path& project_dir,
                                                                  const ComponentRegistry& registry) {
  auto project_text = read_file(project_dir / "project.json");
  if (!project_text) {
    return std::unexpected(project_text.error());
  }
  auto project = load_project_from_json(*project_text);
  if (!project) {
    return std::unexpected(project.error());
  }

  ProjectSession session;
  session.project_dir_ = project_dir;
  session.project_ = std::move(*project);

  auto scene_text = read_file(session.main_scene_path());
  if (!scene_text) {
    return std::unexpected(scene_text.error());
  }
  auto scene = Scene::load_from_json(*scene_text, registry);
  if (!scene) {
    return std::unexpected(scene.error());
  }
  session.scene_ = std::move(*scene);
  session.dirty_ = false;
  return session;
}

std::expected<ProjectSession, std::string> ProjectSession::create(const std::filesystem::path& project_dir,
                                                                    const std::string& name,
                                                                    const ComponentRegistry& registry) {
  std::error_code ec;
  std::filesystem::create_directories(project_dir / "scenes", ec);
  std::filesystem::create_directories(project_dir / "assets", ec);

  ProjectSession session;
  session.project_dir_ = project_dir;
  session.project_.name = name;
  session.project_.main_scene = "scenes/main.scene.json";
  session.dirty_ = true;

  auto saved = session.save(registry);
  if (!saved) {
    return std::unexpected(saved.error());
  }
  return session;
}

std::expected<void, std::string> ProjectSession::save(const ComponentRegistry& registry) {
  std::error_code ec;
  std::filesystem::create_directories(main_scene_path().parent_path(), ec);

  auto wrote_project = write_file(project_dir_ / "project.json", save_project_to_json(project_));
  if (!wrote_project) {
    return std::unexpected(wrote_project.error());
  }

  auto wrote_scene = write_file(main_scene_path(), scene_.save_to_json(registry));
  if (!wrote_scene) {
    return std::unexpected(wrote_scene.error());
  }

  dirty_ = false;
  return {};
}

}  // namespace arcane::editor
