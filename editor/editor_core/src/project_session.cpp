#include "arcane_editor/project_session.hpp"

#include "arcane/canonical_json.hpp"

namespace arcane::editor {

std::expected<ProjectSession, std::string> ProjectSession::open(const std::filesystem::path& project_dir,
                                                                  const ComponentRegistry& registry,
                                                                  SceneLoadPolicy policy) {
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
  auto scene = Scene::load_from_json_strict(*scene_text, registry);
  if (!scene && policy == SceneLoadPolicy::Compatible) {
    session.unsafe_save_reason_ = session.main_scene_path().string() + ": " + scene.error();
    scene = Scene::load_from_json(*scene_text, registry);
  }
  if (!scene) {
    return std::unexpected(scene.error());
  }
  session.scene_ = std::move(*scene);
  auto strict_project = load_project_from_json_strict(*project_text);
  if (!strict_project && session.unsafe_save_reason_.empty()) {
    session.unsafe_save_reason_ = "project.json: " + strict_project.error();
  }
  session.project_revision_ = *project_text;
  session.scene_revision_ = *scene_text;
  session.scene_revision_path_ = session.main_scene_path();
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
  if (!unsafe_save_reason_.empty()) {
    return std::unexpected("Cannot safely save a compatible-loaded project: " + unsafe_save_reason_);
  }
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

  // Check both inputs before either write. This detects edits since open or the
  // last successful write; it is not a lock against a simultaneous writer.
  auto check_revision = [](const std::filesystem::path& path,
                           const std::optional<std::string>& revision) -> std::expected<void, std::string> {
    std::error_code status_error;
    const bool exists = std::filesystem::exists(path, status_error);
    if (status_error) return std::unexpected(path.string() + ": " + status_error.message());
    if (!revision) {
      if (exists) return std::unexpected(path.string() + ": file appeared since opening project");
      return {};
    }
    auto current = read_text_file(path);
    if (!current) return std::unexpected(current.error());
    if (*current != *revision) return std::unexpected(path.string() + ": file changed since opening project");
    return {};
  };
  if (scene_revision_ && scene_revision_path_ != main_scene_path()) {
    return std::unexpected("Main scene path changed since opening project; reopen before saving");
  }
  if (auto checked = check_revision(project_dir_ / "project.json", project_revision_); !checked) return checked;
  if (auto checked = check_revision(main_scene_path(), scene_revision_); !checked) return checked;

  FileTransactionOptions options;
  options.fail_commit_index = fail_commit_index_;
  auto wrote = write_text_files_transactional(
      {{project_dir_ / "project.json", *project_text}, {main_scene_path(), *scene_text}}, options);
  if (!wrote) {
    return std::unexpected(wrote.error());
  }

  project_revision_ = *project_text;
  scene_revision_ = *scene_text;
  scene_revision_path_ = main_scene_path();
  dirty_ = false;
  return {};
}

}  // namespace arcane::editor
