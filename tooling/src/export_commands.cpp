// export: a runnable game folder OUT/<runtime files> + OUT/game/{project.json,scenes,assets,<module>}
// (docs/commands.md, docs/decisions.md row 30). Built in a sibling temp directory and renamed into place,
// so a failure leaves OUT untouched.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <set>

#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/project.hpp"
#include "arcane_tooling/context.hpp"
#include "arcane_tooling/offline.hpp"
#include "offline_internal.hpp"

namespace arcane::tooling {

namespace {

namespace fs = std::filesystem;
using Result = std::expected<Json, ToolError>;

struct Copy {
  fs::path from;
  std::string to;  // OUT-relative '/' path
};

// Removes the directory on destruction unless released.
struct DirGuard {
  fs::path dir;
  ~DirGuard() {
    if (dir.empty()) return;
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

bool inside(const fs::path& path, const fs::path& base) {
  const auto rel = path.lexically_relative(base);
  return !rel.empty() && *rel.begin() != "..";
}

// Regular files below `dir` as copies to `prefix/<relative path>`.
std::expected<void, ToolError> collect_tree(const fs::path& dir, const std::string& prefix, std::vector<Copy>& out) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return {};
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    out.push_back({it->path(), prefix + "/" + fs::relative(it->path(), dir, ec).generic_string()});
  }
  if (ec) return std::unexpected(offline_error("IO_ERROR", "cannot read " + dir.string() + ": " + ec.message()));
  return {};
}

fs::path unique_sibling(const fs::path& out, const char* tag) {
  static std::atomic<unsigned> counter{0};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return out.parent_path() / (out.filename().string() + tag + std::to_string(stamp % 1000003) + "-" + std::to_string(counter++));
}

Result export_game(const OfflineEnv& env, const Json& args, bool dry_run) {
  const fs::path out = args["out"].get<std::string>();
  if (out.filename().empty() || out.filename() == "." || out.filename() == "..") {
    return std::unexpected(offline_error("ARGS_INVALID", "out must name a folder", "", "/out"));
  }
  const bool overwrite = args.value("overwrite", false);
  const bool validate = args.value("validate", true);
  const fs::path runtime_dir = args.value("runtime_dir", "");
  std::error_code ec;
  if (args.contains("runtime_dir") && !fs::is_directory(runtime_dir, ec)) {
    return std::unexpected(offline_error("IO_ERROR", "runtime_dir is not a directory: " + runtime_dir.string(), "", "/runtime_dir"));
  }

  if (validate) {
    auto context = ProjectContext::open(env.project_dir, env.module_override, env.scene_override);
    if (!context) return std::unexpected(context.error());
  }
  if (auto ok = offline_require_project(env); !ok) return std::unexpected(ok.error());
  auto project_text = read_text_file(env.project_dir / "project.json");
  if (!project_text) return std::unexpected(offline_error("IO_ERROR", project_text.error(), "project.json"));
  auto project = load_project_from_json_strict(*project_text);
  if (!project) return std::unexpected(offline_error("SCHEMA_INVALID", project.error(), "project.json"));

  fs::path module_src;
  if (!env.module_override.empty()) module_src = env.module_override;
  else if (!project->module.empty()) module_src = env.project_dir / project->module;
  if (!module_src.empty() && !fs::is_regular_file(module_src, ec)) {
    return std::unexpected(offline_error("MODULE_LOAD_FAILED", "game module file not found: " + module_src.string(), "project.json", "/module",
                                         "Build the game module or pass --module PATH"));
  }
  project->module = module_src.empty() ? "" : module_src.filename().string();

  // OUT must not be the project, one of its parents, or inside the folders being copied.
  const fs::path out_abs = fs::weakly_canonical(out, ec), project_abs = fs::weakly_canonical(env.project_dir, ec);
  if (out_abs == project_abs || inside(project_abs, out_abs) || inside(out_abs, project_abs / "scenes") ||
      inside(out_abs, project_abs / "assets")) {
    return std::unexpected(offline_error("ARGS_INVALID", "out overlaps the project folder", "", "/out"));
  }
  if (fs::exists(out, ec)) {
    if (!fs::is_directory(out, ec)) return std::unexpected(offline_error("CONFLICT", out.string() + " exists and is not a folder", "", "/out"));
    if (!overwrite && !fs::is_empty(out, ec)) {
      return std::unexpected(offline_error("CONFLICT", out.string() + " is not empty", "", "/out", "Pass --overwrite true to replace it"));
    }
  }

  std::vector<Copy> copies;
  if (!runtime_dir.empty()) {
    for (const auto& entry : fs::directory_iterator(runtime_dir, ec)) {
      if (entry.is_regular_file(ec)) copies.push_back({entry.path(), entry.path().filename().generic_string()});
    }
    if (ec) return std::unexpected(offline_error("IO_ERROR", "cannot read " + runtime_dir.string() + ": " + ec.message(), "", "/runtime_dir"));
  }
  const std::size_t runtime_files = copies.size();
  for (const char* folder : {"scenes", "assets"}) {
    if (auto ok = collect_tree(env.project_dir / folder, std::string("game/") + folder, copies); !ok) return std::unexpected(ok.error());
  }
  if (!module_src.empty()) copies.push_back({module_src, "game/" + project->module});
  std::sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) { return a.to < b.to; });

  std::set<std::string> files{"game/project.json"};
  for (const auto& copy : copies) {
    if (!files.insert(copy.to).second) {
      return std::unexpected(offline_error("CONFLICT", "two files export to '" + copy.to + "'", "", "", "Rename the module or the runtime file"));
    }
  }
  if (!files.count(project->main_scene.empty() ? "" : "game/" + project->main_scene)) {
    return std::unexpected(offline_error("VALUE_INVALID", "main scene '" + project->main_scene + "' is not under scenes/ or assets/", "project.json",
                                         "/main_scene", "export copies only scenes/ and assets/"));
  }
  auto canonical = save_project_canonical_json(*project);
  if (!canonical) return std::unexpected(offline_error("VALUE_INVALID", canonical.error(), "project.json"));

  Json file_list = Json::array();
  for (const auto& f : files) file_list.push_back(f);
  Json result = {{"out", out.generic_string()}, {"game_dir", (out / "game").generic_string()},
                 {"module", module_src.empty() ? Json(nullptr) : Json(project->module)}, {"files", file_list},
                 {"runtime_files", runtime_files}};
  if (dry_run) return result;

  if (!out.parent_path().empty()) fs::create_directories(out.parent_path(), ec);
  const fs::path tmp = unique_sibling(out, ".tmp-");
  DirGuard guard{tmp};
  fs::create_directories(tmp / "game", ec);
  if (ec) return std::unexpected(offline_error("IO_ERROR", "cannot create " + tmp.string() + ": " + ec.message()));
  if (auto ok = write_text_file_atomic(tmp / "game" / "project.json", *canonical); !ok) {
    return std::unexpected(offline_error("IO_ERROR", ok.error(), "project.json"));
  }
  for (const auto& copy : copies) {
    const fs::path target = tmp / fs::path(copy.to);
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(copy.from, target, fs::copy_options::none, ec);
    if (ec) return std::unexpected(offline_error("IO_ERROR", "cannot copy " + copy.from.string() + ": " + ec.message(), copy.to));
  }

  fs::path old;
  if (fs::exists(out, ec)) {
    old = unique_sibling(out, ".old-");
    fs::rename(out, old, ec);
    if (ec) return std::unexpected(offline_error("IO_ERROR", "cannot replace " + out.string() + ": " + ec.message(), "", "/out"));
  }
  fs::rename(tmp, out, ec);
  if (ec) {
    const std::string message = ec.message();
    if (!old.empty()) fs::rename(old, out, ec);
    return std::unexpected(offline_error("IO_ERROR", "cannot move the export into place: " + message, "", "/out"));
  }
  guard.dir.clear();
  if (!old.empty()) fs::remove_all(old, ec);
  return result;
}

}  // namespace

std::vector<OfflineCommand> make_export_commands() {
  const Json kStr = {{"type", "string"}}, kBool = {{"type", "boolean"}};
  std::vector<OfflineCommand> c;
  c.push_back({{"export",
                "Export a runnable game folder: OUT/<runtime files> + OUT/game/{project.json,scenes,assets,<module>}. runtime_dir's regular files "
                "(e.g. `cmake --install BUILD --prefix DIR --component game`) are copied flat into OUT; project.json is rewritten canonically with "
                "`module` set to the module file's name. Built in OUT.tmp-* and renamed, so a failure leaves OUT untouched. A non-empty OUT needs overwrite. "
                "validate (default true) opens the project strictly first.",
                offline_object_schema({{"out", kStr}, {"runtime_dir", kStr}, {"overwrite", kBool}, {"validate", kBool}}, {"out"}, {"out"}),
                {{"type", "object"}},
                true},
               export_game});
  return c;
}

}  // namespace arcane::tooling
