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
  // by build-tree tests). Fails with PROJECT_NOT_FOUND, MODULE_LOAD_FAILED
  // or SCHEMA_INVALID; nothing is left loaded on failure.
  static std::expected<std::unique_ptr<ProjectContext>, ToolError> open(const std::filesystem::path& project_dir,
                                                                        const std::string& module_override = {});

  ~ProjectContext();
  ProjectContext(const ProjectContext&) = delete;
  ProjectContext& operator=(const ProjectContext&) = delete;
  ProjectContext(ProjectContext&&) = delete;
  ProjectContext& operator=(ProjectContext&&) = delete;

  const std::filesystem::path& project_dir() const noexcept;
  ComponentRegistry& registry() noexcept;
  Scene& scene() noexcept;
  // Project-relative path of the scene the commands edit.
  std::string scene_file() const;

  bool dirty() const noexcept;
  void mark_dirty() noexcept;

  // Writes project.json and the scene atomically per file (both are
  // serialized before either is written). Fails with CONFLICT, writing
  // nothing, if the scene file on disk no longer matches what was loaded or
  // last saved by this context.
  std::expected<void, ToolError> save();

 private:
  ProjectContext(editor::ProjectRuntime&& runtime, std::string scene_text);

  editor::ProjectRuntime runtime_;
  std::string loaded_scene_text_;
};

}  // namespace arcane::tooling
