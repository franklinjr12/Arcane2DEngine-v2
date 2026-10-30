#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "arcane_tooling/error.hpp"

namespace arcane::tooling {

// One open project for command execution: builtin and project-module
// component types are registered before the scene is read (via
// editor::ProjectRuntime, the same path as the editor and player), and the
// module stays loaded for as long as the context lives. Commands must not
// keep Node/Component pointers or descriptors past the context's lifetime;
// the destructor destroys the scene and registry before the module is
// unloaded.
class ProjectContext {
 public:
  // `module_override`, if non-empty, replaces project.json's "module" (used
  // by build-tree tests). `policy` is Strict except for the editor, which keeps
  // opening legacy sample projects (its Save then refuses to drop unknown data
  // itself). The scene is loaded strictly by default: a legacy file with
  // fields this engine does not know is refused (SCHEMA_INVALID) instead of
  // being loaded leniently and losing them on save. Fails with
  // PROJECT_NOT_FOUND, MODULE_LOAD_FAILED or SCHEMA_INVALID; nothing is left
  // loaded on failure.
  //
  // `scene_override`, if non-empty, is a project-relative '/' path of another
  // scene file to edit instead of project.json's main scene (the main scene
  // must still load). It is read strictly too, and save() writes that file.
  static std::expected<std::unique_ptr<ProjectContext>, ToolError> open(const std::filesystem::path& project_dir,
                                                                        const std::string& module_override = {},
                                                                        const std::string& scene_override = {},
                                                                        editor::SceneLoadPolicy policy =
                                                                            editor::SceneLoadPolicy::Strict);

  ~ProjectContext();
  ProjectContext(const ProjectContext&) = delete;
  ProjectContext& operator=(const ProjectContext&) = delete;
  ProjectContext(ProjectContext&&) = delete;
  ProjectContext& operator=(ProjectContext&&) = delete;

  const std::filesystem::path& project_dir() const noexcept;
  ComponentRegistry& registry() noexcept;
  Scene& scene() noexcept;
  // The editor's session for the same project (project.json data, assets dir, whole-project save).
  editor::ProjectSession& session() noexcept { return runtime_.session(); }
  // Project-relative path of the scene the commands edit.
  const std::string& scene_file() const noexcept { return scene_file_; }

  bool dirty() const noexcept { return dirty_; }
  void mark_dirty() noexcept { dirty_ = true; }

  // Project-level data (project.json: HUD, input maps ...) is edited in memory through
  // session().mutable_project(); commands that change it call mark_project_dirty().
  bool project_dirty() const noexcept { return project_dirty_; }
  void mark_project_dirty() noexcept { project_dirty_ = true; }
  void clear_project_dirty() noexcept { project_dirty_ = false; }  // only after restoring the loaded state

  // Writes the scene file and, when project_dirty(), project.json. Everything is serialized
  // before the first byte is written, project.json is written first, then the scene; each
  // replace is atomic, but the pair is not a transaction (a failure of the second write leaves
  // the new project.json behind). Fails with CONFLICT, writing nothing, if a file to be
  // written no longer matches what was loaded or last saved by this context.
  std::expected<void, ToolError> save();

 private:
  ProjectContext(editor::ProjectRuntime&& runtime, std::string scene_file, std::string scene_text,
                 std::string project_text);

  // runtime_ owns the module, registry and scene; it is destroyed with the
  // context, scene and registry first, module last.
  editor::ProjectRuntime runtime_;
  std::string scene_file_;
  std::string loaded_scene_text_;
  std::string loaded_project_text_;
  bool dirty_ = false;
  bool project_dirty_ = false;
};

}  // namespace arcane::tooling
