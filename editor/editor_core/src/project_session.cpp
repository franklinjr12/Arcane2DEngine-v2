#include "arcane_editor/project_session.hpp"

#include "arcane/canonical_json.hpp"

namespace arcane::editor {

std::expected<ProjectSession, std::string> ProjectSession::open(const std::filesystem::path& project_dir,
                                                                  const ComponentRegistry& registry) {
  auto project_text = read_text_file(project_dir / "project.json");
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

  auto scene_text = read_text_file(session.main_scene_path());
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

  // Serialize both documents before touching disk so a serialization error
  // (e.g. a NaN property) never leaves a half-saved project.
  auto project_text = save_project_canonical_json(project_);
  if (!project_text) {
    return std::unexpected("project.json: " + project_text.error());
  }
  auto scene_text = scene_.save_canonical_json(registry);
  if (!scene_text) {
    return std::unexpected(main_scene_path().string() + ": " + scene_text.error());
  }

  auto wrote_project = write_text_file_atomic(project_dir_ / "project.json", *project_text);
  if (!wrote_project) {
    return std::unexpected(wrote_project.error());
  }

  auto wrote_scene = write_text_file_atomic(main_scene_path(), *scene_text);
  if (!wrote_scene) {
    return std::unexpected(wrote_scene.error());
  }

  dirty_ = false;
  return {};
}

}  // namespace arcane::editor
