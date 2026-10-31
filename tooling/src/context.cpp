#include "arcane_tooling/context.hpp"

#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/prefab.hpp"
#include "arcane/project.hpp"

namespace arcane::tooling {

namespace {

// ProjectRuntime::open reports module failures with this prefix.
constexpr const char* kModuleFailurePrefix = "failed to load project module";

}  // namespace

ProjectContext::ProjectContext(editor::ProjectRuntime&& runtime, std::string scene_file, std::string scene_text,
                               std::string project_text)
    : runtime_(std::move(runtime)),
      scene_file_(std::move(scene_file)),
      loaded_scene_text_(std::move(scene_text)),
      loaded_project_text_(std::move(project_text)) {}

ProjectContext::~ProjectContext() = default;

std::expected<std::unique_ptr<ProjectContext>, ToolError> ProjectContext::open(
    const std::filesystem::path& project_dir, const std::string& module_override, const std::string& scene_override,
    editor::SceneLoadPolicy policy) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(project_dir / "project.json", ec)) {
    return std::unexpected(ToolError{"PROJECT_NOT_FOUND", "No project.json in " + project_dir.string(), "", "",
                                     "Pass --project DIR pointing at a directory containing project.json"});
  }
  auto runtime = editor::ProjectRuntime::open(project_dir, module_override, policy);
  if (!runtime) {
    const std::string& message = runtime.error();
    if (message.rfind(kModuleFailurePrefix, 0) == 0) {
      return std::unexpected(ToolError{"MODULE_LOAD_FAILED", message, "", "",
                                       "Build the game module or pass --module PATH"});
    }
    return std::unexpected(ToolError{"SCHEMA_INVALID", message, "", "",
                                     "Fix the file or migrate it to format v2 (unknown data is never dropped)"});
  }
  std::string scene_file = runtime->session().project().main_scene;
  std::filesystem::path scene_path = runtime->session().main_scene_path();
  if (!scene_override.empty() && scene_override != scene_file) {
    if (!is_project_relative_path(scene_override)) {
      return std::unexpected(ToolError{"ARGS_INVALID", "'" + scene_override + "' is not a project-relative '/' path", "",
                                       "/scene", "Use a path like scenes/level1.scene.json"});
    }
    scene_file = scene_override;
    scene_path = project_dir / scene_override;
  }
  auto scene_text = read_text_file(scene_path);
  if (!scene_text) {
    return std::unexpected(ToolError{"IO_ERROR", scene_text.error(), scene_file, "", "Create it with scene new"});
  }
  if (scene_file != runtime->session().project().main_scene) {
    auto scene = Scene::load_from_json_strict(*scene_text, runtime->registry());
    if (!scene) {
      return std::unexpected(ToolError{"SCHEMA_INVALID", scene.error(), scene_file, "",
                                       "Fix the file or migrate it to format v2 (unknown data is never dropped)"});
    }
    refresh_prefab_instances(*scene, project_dir, runtime->registry());
    runtime->session().scene() = std::move(*scene);
  }
  auto project_text = read_text_file(project_dir / "project.json");
  if (!project_text) {
    return std::unexpected(ToolError{"IO_ERROR", project_text.error(), "project.json", "", ""});
  }
  return std::unique_ptr<ProjectContext>(new ProjectContext(std::move(*runtime), std::move(scene_file),
                                                            std::move(*scene_text), std::move(*project_text)));
}

const std::filesystem::path& ProjectContext::project_dir() const noexcept {
  return runtime_.session().project_dir();
}
ComponentRegistry& ProjectContext::registry() noexcept { return runtime_.registry(); }
Scene& ProjectContext::scene() noexcept { return runtime_.session().scene(); }

std::expected<void, ToolError> ProjectContext::save() {
  const std::filesystem::path path = project_dir() / scene_file_;
  const std::filesystem::path project_path = project_dir() / "project.json";
  // Serialize everything first so a validation failure writes nothing.
  auto text = scene().save_canonical_json(registry());
  if (!text) {
    return std::unexpected(ToolError{"VALUE_INVALID", text.error(), scene_file(), "", ""});
  }
  std::string project_text;
  if (project_dirty_) {
    auto serialized = save_project_canonical_json(session().project());
    if (!serialized) {
      return std::unexpected(ToolError{"VALUE_INVALID", serialized.error(), "project.json", "", ""});
    }
    project_text = std::move(*serialized);
  }
  // Refuse to overwrite a file somebody else edited since we read it. This
  // detects edits, it is not a lock against a simultaneous writer.
  auto on_disk = read_text_file(path);
  if (!on_disk) {
    return std::unexpected(ToolError{"IO_ERROR", on_disk.error(), scene_file(), "", ""});
  }
  if (*on_disk != loaded_scene_text_) {
    return std::unexpected(ToolError{"CONFLICT", "Scene file changed on disk since it was loaded", scene_file(), "",
                                     "Reopen the project and repeat the command"});
  }
  if (project_dirty_) {
    auto project_on_disk = read_text_file(project_path);
    if (!project_on_disk) {
      return std::unexpected(ToolError{"IO_ERROR", project_on_disk.error(), "project.json", "", ""});
    }
    if (*project_on_disk != loaded_project_text_) {
      return std::unexpected(ToolError{"CONFLICT", "project.json changed on disk since it was loaded", "project.json",
                                       "", "Reopen the project and repeat the command"});
    }
  }
  std::vector<FileWrite> writes;
  if (project_dirty_) writes.push_back({project_path, project_text});
  writes.push_back({path, *text});
  FileTransactionOptions options;
  options.fail_commit_index = fail_commit_index_;
  if (auto written = write_text_files_transactional(writes, options); !written) {
    return std::unexpected(ToolError{"IO_ERROR", written.error(), project_dirty_ ? "project.json and " + scene_file() : scene_file(), "", ""});
  }
  if (project_dirty_) {
    loaded_project_text_ = std::move(project_text);
    project_dirty_ = false;
  }
  loaded_scene_text_ = std::move(*text);
  dirty_ = false;
  return {};
}

}  // namespace arcane::tooling
