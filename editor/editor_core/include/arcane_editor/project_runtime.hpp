#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "arcane/component_registry.hpp"
#include "arcane/game_api.hpp"
#include "arcane/module.hpp"
#include "arcane_editor/project_session.hpp"

namespace arcane::editor {

// Opens a project for either the editor or the player: builds a
// ComponentRegistry with builtin component types plus (if the project
// declares one, or `module_override` is given) the project's native
// module's custom component types registered, then opens the
// ProjectSession against that fully-populated registry. Shared so both
// `arcane_editor` and `arcane_player --project` get the same module
// loading/registration/destruction-order behavior rather than duplicating
// it (see docs/decisions.md).
class ProjectRuntime {
 public:
  // `module_override`, if non-empty, replaces project.json's "module"
  // field; used by both executables' gl-labelled CTest entries so each
  // build config loads the module it just built rather than a shared copy
  // in the source tree a different config could have last overwritten.
  static std::expected<ProjectRuntime, std::string> open(const std::filesystem::path& project_dir,
                                                           const std::string& module_override = {},
                                                           SceneLoadPolicy policy = SceneLoadPolicy::Compatible);

  ProjectRuntime(ProjectRuntime&&) noexcept = default;
  ProjectRuntime& operator=(ProjectRuntime&&) noexcept = default;
  ProjectRuntime(const ProjectRuntime&) = delete;
  ProjectRuntime& operator=(const ProjectRuntime&) = delete;
  ~ProjectRuntime();

  ComponentRegistry& registry() noexcept { return registry_; }
  ProjectSession& session() noexcept { return *session_; }
  const ProjectSession& session() const noexcept { return *session_; }

  // nullptr if the project declares no module.
  const GameApi* module_api() const noexcept { return module_ ? module_->api() : nullptr; }

 private:
  ProjectRuntime() = default;

  // Declaration order matters and must not be "cleaned up": members are
  // destroyed in reverse declaration order, so `module_` (declared first)
  // is destroyed *last* -- after `session_`'s Scene (which may hold
  // module-type Components with vtables living in the module's .so) and
  // `registry_`'s PropertyDescriptors (std::function closures compiled
  // into the module) have already been torn down. Destroying the module
  // first crashes inside ~_Function_base with a SIGSEGV. See
  // docs/decisions.md.
  std::optional<GameModule> module_;
  ComponentRegistry registry_;
  std::optional<ProjectSession> session_;
};

}  // namespace arcane::editor
