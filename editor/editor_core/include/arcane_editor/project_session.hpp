#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <optional>

#include "arcane/component_registry.hpp"
#include "arcane/project.hpp"
#include "arcane/scene.hpp"

namespace arcane::editor {

enum class SceneLoadPolicy { Compatible, Strict };

// Owns the editor's view of an on-disk project: the parsed ProjectFile, the
// currently loaded (authored) Scene, and a dirty flag. Wraps project.hpp's
// and Scene's pure JSON (de)serialization with the file I/O and path
// resolution the editor/player actually need; doesn't duplicate any
// scene/project format logic.
class ProjectSession {
 public:
  static std::expected<ProjectSession, std::string> open(const std::filesystem::path& project_dir,
                                                           const ComponentRegistry& registry,
                                                           SceneLoadPolicy policy = SceneLoadPolicy::Compatible);

  // Creates a brand-new project on disk: project.json + an empty main scene
  // under scenes/.
  static std::expected<ProjectSession, std::string> create(const std::filesystem::path& project_dir,
                                                             const std::string& name,
                                                             const ComponentRegistry& registry);

  ProjectSession(ProjectSession&&) noexcept = default;
  ProjectSession& operator=(ProjectSession&&) noexcept = default;
  ProjectSession(const ProjectSession&) = delete;
  ProjectSession& operator=(const ProjectSession&) = delete;

  // Writes project.json and the current scene back to main_scene_path();
  // clears the dirty flag on success.
  std::expected<void, std::string> save(const ComponentRegistry& registry);

  const ProjectFile& project() const noexcept { return project_; }
  // For editor panels that edit project-level data (e.g. the HUD); call mark_dirty() after changes.
  ProjectFile& mutable_project() noexcept { return project_; }
  const std::filesystem::path& project_dir() const noexcept { return project_dir_; }
  std::filesystem::path assets_dir() const { return project_dir_ / "assets"; }
  std::filesystem::path main_scene_path() const { return project_dir_ / project_.main_scene; }

  Scene& scene() noexcept { return scene_; }
  const Scene& scene() const noexcept { return scene_; }

  bool dirty() const noexcept { return dirty_; }
  void mark_dirty() noexcept { dirty_ = true; }

 private:
  ProjectSession() = default;

  ProjectFile project_;
  std::filesystem::path project_dir_;
  Scene scene_;
  bool dirty_ = false;
  // Compatible loading may discard fields that this engine does not know.
  // Keep the strict-load diagnostic so Save cannot silently erase them.
  std::string unsafe_save_reason_;
  std::optional<std::string> project_revision_;
  std::optional<std::string> scene_revision_;
  std::filesystem::path scene_revision_path_;
};

}  // namespace arcane::editor
