#include "arcane_tooling/context.hpp"

#include "arcane/canonical_json.hpp"

namespace arcane::tooling {

namespace {

// ProjectRuntime::open reports module failures with this prefix.
constexpr const char* kModuleFailurePrefix = "failed to load project module";

}  // namespace

ProjectContext::ProjectContext(editor::ProjectRuntime&& runtime, std::string scene_text)
    : runtime_(std::move(runtime)), loaded_scene_text_(std::move(scene_text)) {}

ProjectContext::~ProjectContext() = default;

std::expected<std::unique_ptr<ProjectContext>, ToolError> ProjectContext::open(
    const std::filesystem::path& project_dir, const std::string& module_override) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(project_dir / "project.json", ec)) {
    return std::unexpected(ToolError{"PROJECT_NOT_FOUND", "No project.json in " + project_dir.string(), "", "",
                                     "Pass --project DIR pointing at a directory containing project.json"});
  }
  auto runtime = editor::ProjectRuntime::open(project_dir, module_override, editor::SceneLoadPolicy::Strict);
  if (!runtime) {
    const std::string& message = runtime.error();
    if (message.rfind(kModuleFailurePrefix, 0) == 0) {
      return std::unexpected(ToolError{"MODULE_LOAD_FAILED", message, "", "",
                                       "Build the game module or pass --module PATH"});
    }
    return std::unexpected(ToolError{"SCHEMA_INVALID", message, "", "",
                                     "Fix the file or migrate it to format v2 (unknown data is never dropped)"});
  }
  auto scene_text = read_text_file(runtime->session().main_scene_path());
  if (!scene_text) {
    return std::unexpected(ToolError{"IO_ERROR", scene_text.error(), "", "", ""});
  }
  return std::unique_ptr<ProjectContext>(new ProjectContext(std::move(*runtime), std::move(*scene_text)));
}

const std::filesystem::path& ProjectContext::project_dir() const noexcept {
  return runtime_.session().project_dir();
}
ComponentRegistry& ProjectContext::registry() noexcept { return runtime_.registry(); }
Scene& ProjectContext::scene() noexcept { return runtime_.session().scene(); }
std::string ProjectContext::scene_file() const { return runtime_.session().project().main_scene; }

std::expected<void, ToolError> ProjectContext::save() {
  const std::filesystem::path path = runtime_.session().main_scene_path();
  auto text = scene().save_canonical_json(registry());
  if (!text) {
    return std::unexpected(ToolError{"VALUE_INVALID", text.error(), scene_file(), "", ""});
  }
  // Refuse to overwrite a scene somebody else edited since we read it. This
  // detects edits, it is not a lock against a simultaneous writer.
  auto on_disk = read_text_file(path);
  if (!on_disk) {
    return std::unexpected(ToolError{"IO_ERROR", on_disk.error(), scene_file(), "", ""});
  }
  if (*on_disk != loaded_scene_text_) {
    return std::unexpected(ToolError{"CONFLICT", "Scene file changed on disk since it was loaded", scene_file(), "",
                                     "Reopen the project and repeat the command"});
  }
  if (auto written = write_text_file_atomic(path, *text); !written) {
    return std::unexpected(ToolError{"IO_ERROR", written.error(), scene_file(), "", ""});
  }
  loaded_scene_text_ = std::move(*text);
  dirty_ = false;
  return {};
}

}  // namespace arcane::tooling
