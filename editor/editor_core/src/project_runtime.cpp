#include "arcane_editor/project_runtime.hpp"

#include <fstream>
#include <sstream>

#include "arcane/animation.hpp"
#include "arcane/tween.hpp"
#include "arcane/audio.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/physics.hpp"
#include "arcane/project.hpp"
#include "arcane/particles.hpp"
#include "arcane/tilemap.hpp"

namespace arcane::editor {

std::expected<ProjectRuntime, std::string> ProjectRuntime::open(const std::filesystem::path& project_dir,
                                                                  const std::string& module_override,
                                                                  SceneLoadPolicy policy) {
  ProjectRuntime runtime;
  register_builtin_components(runtime.registry_);
  register_physics_components(runtime.registry_);
  register_animation_components(runtime.registry_);
  register_tween_components(runtime.registry_);
  register_audio_components(runtime.registry_);
  register_tilemap_components(runtime.registry_);
  register_particle_components(runtime.registry_);

  std::optional<std::filesystem::path> module_path;
  if (!module_override.empty()) {
    module_path = module_override;
  } else {
    std::ifstream in(project_dir / "project.json");
    std::ostringstream ss;
    ss << in.rdbuf();
    auto probe = load_project_from_json(ss.str());
    if (probe && !probe->module.empty()) {
      module_path = project_dir / probe->module;
    }
  }

  if (module_path) {
    auto loaded = GameModule::load(*module_path);
    if (!loaded) {
      return std::unexpected("failed to load project module '" + module_path->string() + "': " + loaded.error());
    }
    runtime.module_ = std::move(*loaded);
    if (runtime.module_->api()->register_types) {
      runtime.module_->api()->register_types(runtime.registry_);
    }
    runtime.module_->api()->on_load();
  }

  auto session = ProjectSession::open(project_dir, runtime.registry_, policy);
  if (!session) {
    return std::unexpected(session.error());
  }
  runtime.session_ = std::move(*session);
  return runtime;
}

ProjectRuntime::~ProjectRuntime() {
  // module_ may be a moved-from GameModule (api() == nullptr) if this
  // ProjectRuntime was itself moved out of -- std::optional's move
  // constructor leaves the source engaged (has_value() still true), it
  // just moves the GameModule inside, which nulls out its own api_/handle_.
  if (module_ && module_->api()) {
    module_->api()->on_unload();
  }
}

}  // namespace arcane::editor
