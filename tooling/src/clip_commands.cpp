// clip.* commands (docs/commands.md): slice a sprite sheet grid, create clips and edit their frames. They read and
// write animation clip asset files only, like atlas.* and tileset.*: the open scene is never touched and the file is
// written at once (no persist/dry_run). Every edit returns clip.restore with the previous file bytes as its inverse.
// The sheet's pixel size is read from the PNG file (no GL context); other image formats are not supported.
#include <algorithm>
#include <filesystem>
#include <optional>

#include "arcane/animation.hpp"
#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/screenshot.hpp"
#include "arcane_editor/clip_authoring.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

namespace fs = std::filesystem;
using editor::ClipDraft;
using editor::SheetGrid;

ToolError fail(const char* code, std::string message, std::string path = {}, std::string hint = {}, std::string file = {}) {
  return ToolError{code, std::move(message), std::move(file), std::move(path), std::move(hint)};
}

Json schema(Json properties, std::initializer_list<const char*> required) {
  Json req = Json::array();
  for (const char* r : required) req.push_back(r);
  return {{"type", "object"}, {"properties", std::move(properties)}, {"required", req}, {"additionalProperties", false}};
}

const Json kString = {{"type", "string"}};
const Json kBool = {{"type", "boolean"}};
const Json kIndex = {{"type", "integer"}, {"minimum", 0}};
const Json kCell = {{"type", "integer"}, {"minimum", 0}};
const Json kSeconds = {{"type", "number"}, {"minimum", 0}};

// Grid arguments shared by the sheet-based commands.
Json with_grid(Json properties) {
  properties["columns"] = {{"type", "integer"}, {"minimum", 1}};
  properties["rows"] = {{"type", "integer"}, {"minimum", 1}};
  properties["frame_width"] = {{"type", "integer"}, {"minimum", 0}};
  properties["frame_height"] = {{"type", "integer"}, {"minimum", 0}};
  properties["margin"] = {{"type", "integer"}, {"minimum", 0}};
  properties["spacing"] = {{"type", "integer"}, {"minimum", 0}};
  return properties;
}

SheetGrid grid_of(const Json& args) {
  SheetGrid grid;
  grid.columns = args.value("columns", 1);
  grid.rows = args.value("rows", 1);
  grid.frame_width = args.value("frame_width", 0);
  grid.frame_height = args.value("frame_height", 0);
  grid.margin = args.value("margin", 0);
  grid.spacing = args.value("spacing", 0);
  return grid;
}

std::expected<void, ToolError> check_path(const std::string& value, const char* arg) {
  if (is_project_relative_path(value)) return {};
  return std::unexpected(fail("ARGS_INVALID", "'" + value + "' is not a project-relative '/' path", std::string("/") + arg));
}

struct Sheet {
  int width = 0;
  int height = 0;
};

std::expected<Sheet, ToolError> read_sheet(ProjectContext& context, const std::string& file) {
  if (auto ok = check_path(file, "sheet"); !ok) return std::unexpected(ok.error());
  std::error_code ec;
  if (!fs::exists(context.project_dir() / file, ec)) {
    return std::unexpected(fail("IO_ERROR", "sheet " + file + " does not exist", "/sheet", {}, file));
  }
  auto image = read_png(context.project_dir() / file);
  if (!image) {
    return std::unexpected(fail("VALUE_INVALID", "cannot read sheet as PNG: " + image.error(), "/sheet",
                                "clip.* read the sheet size from PNG files only", file));
  }
  return Sheet{image->width, image->height};
}

Json uv_json(const UvRect& uv) { return Json::array({uv.u0, uv.v0, uv.u1, uv.v1}); }

// The clip file as it is on disk right now.
struct ClipFile {
  std::string path;
  std::optional<std::string> text;  // nullopt = absent
};

ClipFile snapshot(ProjectContext& context, const std::string& path) {
  ClipFile file{path, std::nullopt};
  std::error_code ec;
  if (fs::exists(context.project_dir() / path, ec)) {
    if (auto text = read_text_file(context.project_dir() / path)) file.text = std::move(*text);
  }
  return file;
}

Invocation restore_call(const ClipFile& previous) {
  Json args = {{"clip", previous.path}};
  if (previous.text) {
    args["content"] = *previous.text;
  } else {
    args["absent"] = true;
  }
  return Invocation{"clip.restore", std::move(args)};
}

std::expected<void, ToolError> write_clip(ProjectContext& context, const std::string& path, const AnimationClip& clip) {
  auto canonical = save_clip_canonical_json(clip);
  if (!canonical) return std::unexpected(fail("VALUE_INVALID", canonical.error(), "", {}, path));
  const auto target = context.project_dir() / path;
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  if (auto ok = editor::save_clip_file(target, clip); !ok) return std::unexpected(fail("IO_ERROR", ok.error(), "", {}, path));
  return {};
}

std::expected<AnimationClip, ToolError> load_clip(ProjectContext& context, const std::string& path) {
  if (auto ok = check_path(path, "clip"); !ok) return std::unexpected(ok.error());
  std::error_code ec;
  if (!fs::exists(context.project_dir() / path, ec)) {
    return std::unexpected(fail("IO_ERROR", "clip " + path + " does not exist", "/clip", "Create it with clip.create", path));
  }
  auto clip = editor::load_clip_file(context.project_dir() / path);
  if (!clip) return std::unexpected(fail("SCHEMA_INVALID", clip.error(), "/clip", {}, path));
  return std::move(*clip);
}

// A clip reopened on a grid, ready to edit as a draft.
struct Opened {
  std::string path;
  Sheet sheet;
  ClipDraft draft;
  std::string before;  // canonical text of the clip as loaded
};

std::expected<Opened, ToolError> open_on_grid(ProjectContext& context, const Json& args) {
  const std::string path = args["clip"];
  auto clip = load_clip(context, path);
  if (!clip) return std::unexpected(clip.error());
  auto sheet = read_sheet(context, args["sheet"]);
  if (!sheet) return std::unexpected(sheet.error());
  auto draft = editor::draft_from_clip(*clip, grid_of(args), sheet->width, sheet->height);
  if (!draft) {
    return std::unexpected(fail("VALUE_INVALID", "cannot reopen " + path + " on this grid: " + draft.error(), "",
                                "Check the grid with clip.slice / clip.info", path));
  }
  return Opened{path, *sheet, std::move(*draft), save_clip_to_json(*clip)};
}

std::expected<void, ToolError> check_cell(const SheetGrid& grid, int cell, const std::string& where) {
  const int count = grid.columns * grid.rows;
  if (cell >= 0 && cell < count) return {};
  return std::unexpected(fail("VALUE_INVALID", "cell " + std::to_string(cell) + " is outside the grid (0.." +
                                                   std::to_string(count - 1) + ")",
                              where, "Cells are numbered row-major from the top-left; see clip.slice"));
}

// Rebuilds and saves an edited draft. A result equal to the file as loaded is a no-op (no write, no inverse).
std::expected<CommandResult, ToolError> commit(ProjectContext& context, const Opened& opened, Json extra = Json::object()) {
  const AnimationClip clip = editor::build_clip(opened.draft, opened.sheet.width, opened.sheet.height);
  Json value = {{"clip", opened.path}, {"frames", clip.frames.size()}};
  for (auto& [key, v] : extra.items()) value[key] = v;
  if (save_clip_to_json(clip) == opened.before) {
    value["changed"] = false;
    return CommandResult{std::move(value), std::nullopt};
  }
  const ClipFile previous = snapshot(context, opened.path);
  if (auto ok = write_clip(context, opened.path, clip); !ok) return std::unexpected(ok.error());
  value["changed"] = true;
  return CommandResult{std::move(value), restore_call(previous)};
}

std::expected<void, ToolError> check_range(const SheetGrid& grid, const Json& args) {
  if (auto ok = check_cell(grid, args["from"], "/from"); !ok) return ok;
  return check_cell(grid, args["to"], "/to");
}

Json sheet_props(Json extra) {
  Json props = with_grid({{"clip", kString}, {"sheet", kString}});
  for (auto& [key, v] : extra.items()) props[key] = v;
  return props;
}

}  // namespace

void register_clip_commands(CommandRegistry& registry) {
  registry.add(
      {"clip.slice",
       "Cut a sprite sheet into a regular grid: returns the sheet size (read from the PNG) and the UV rect "
       "[u0,v0,u1,v1] of every cell, numbered row-major from the top-left. frame_width/frame_height 0 = auto.",
       schema(with_grid({{"sheet", kString}}), {"sheet", "columns", "rows"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto sheet = read_sheet(context, args["sheet"]);
        if (!sheet) return std::unexpected(sheet.error());
        const SheetGrid grid = grid_of(args);
        Json cells = Json::array();
        for (int i = 0; i < grid.columns * grid.rows; ++i) {
          const UvRect uv = editor::grid_cell_uv(grid, sheet->width, sheet->height, i);
          if (!(uv.u1 > uv.u0) || !(uv.v1 > uv.v0)) {
            return std::unexpected(fail("VALUE_INVALID", "cell " + std::to_string(i) + " has an empty UV rect", "",
                                        "Check frame_width/frame_height against the sheet size"));
          }
          cells.push_back({{"index", i}, {"uv", uv_json(uv)}});
        }
        return CommandResult{{{"sheet", args["sheet"]}, {"sheet_width", sheet->width}, {"sheet_height", sheet->height},
                              {"cells", cells}},
                             std::nullopt};
      });

  registry.add(
      {"clip.info",
       "Describe a clip file: name, loop and frames [{uv, seconds}]. With `sheet` and the grid arguments (columns, rows ...) "
       "it also reopens the clip on that grid and returns `cells`, `durations` and `fps`; frames that are not on the grid are "
       "a VALUE_INVALID error naming the frame.",
       schema(with_grid({{"clip", kString}, {"sheet", kString}}), {"clip"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string path = args["clip"];
        auto clip = load_clip(context, path);
        if (!clip) return std::unexpected(clip.error());
        Json frames = Json::array();
        for (const auto& f : clip->frames) frames.push_back({{"uv", uv_json(f.uv)}, {"seconds", f.duration_seconds}});
        Json value = {{"clip", path}, {"name", clip->name}, {"loop", clip->loop}, {"frames", frames}};
        if (args.contains("sheet")) {
          if (!args.contains("columns") || !args.contains("rows")) {
            return std::unexpected(fail("ARGS_INVALID", "columns and rows are required with sheet", "/columns"));
          }
          auto opened = open_on_grid(context, args);
          if (!opened) return std::unexpected(opened.error());
          value["sheet"] = args["sheet"];
          value["cells"] = opened->draft.cells;
          value["durations"] = opened->draft.durations;
          value["fps"] = opened->draft.fps;
        }
        return CommandResult{std::move(value), std::nullopt};
      });

  registry.add(
      {"clip.create",
       "Create a clip file from sprite sheet grid cells (`cells`, or the inclusive range `from`..`to`), `fps` (default 10) "
       "and optional per-frame `durations` in seconds (0 = 1/fps). Fails if the file exists unless overwrite=true. "
       "Writes the file at once; the inverse is clip.restore.",
       schema(with_grid({{"clip", kString}, {"sheet", kString}, {"cells", {{"type", "array"}}}, {"from", kCell}, {"to", kCell},
                         {"fps", {{"type", "number"}, {"minimum", 0}}}, {"durations", {{"type", "array"}}},
                         {"loop", kBool}, {"name", kString}, {"overwrite", kBool}}),
              {"clip", "sheet", "columns", "rows"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string path = args["clip"];
        if (auto ok = check_path(path, "clip"); !ok) return std::unexpected(ok.error());
        auto sheet = read_sheet(context, args["sheet"]);
        if (!sheet) return std::unexpected(sheet.error());
        ClipDraft draft;
        draft.grid = grid_of(args);
        draft.name = args.value("name", std::string());
        draft.loop = args.value("loop", true);
        draft.fps = args.value("fps", 10.0f);
        if (!(draft.fps > 0.0f)) return std::unexpected(fail("ARGS_INVALID", "fps must be above 0", "/fps"));
        const bool has_cells = args.contains("cells");
        const bool has_range = args.contains("from") || args.contains("to");
        if (has_cells == has_range) {
          return std::unexpected(fail("ARGS_INVALID", "give either cells or from and to", has_cells ? "/cells" : "/from"));
        }
        if (has_range) {
          if (!args.contains("from") || !args.contains("to")) {
            return std::unexpected(fail("ARGS_INVALID", "from and to go together", "/from"));
          }
          if (auto ok = check_range(draft.grid, args); !ok) return std::unexpected(ok.error());
          draft.add_range(args["from"], args["to"]);
        } else {
          for (std::size_t i = 0; i < args["cells"].size(); ++i) {
            const Json& cell = args["cells"][i];
            const std::string where = "/cells/" + std::to_string(i);
            if (!cell.is_number_integer()) return std::unexpected(fail("ARGS_INVALID", "cells are integers", where));
            if (auto ok = check_cell(draft.grid, cell.get<int>(), where); !ok) return std::unexpected(ok.error());
            draft.cells.push_back(cell.get<int>());
          }
        }
        if (draft.cells.empty()) return std::unexpected(fail("ARGS_INVALID", "cells must not be empty", "/cells"));
        if (args.contains("durations")) {
          if (args["durations"].size() != draft.cells.size()) {
            return std::unexpected(fail("ARGS_INVALID", "durations needs one entry per frame (" + std::to_string(draft.cells.size()) + ")",
                                        "/durations"));
          }
          for (std::size_t i = 0; i < args["durations"].size(); ++i) {
            const Json& d = args["durations"][i];
            if (!d.is_number() || d.get<double>() < 0.0) {
              return std::unexpected(fail("ARGS_INVALID", "durations are numbers >= 0", "/durations/" + std::to_string(i)));
            }
            draft.durations.push_back(d.get<float>());
          }
        }
        std::error_code ec;
        if (fs::exists(context.project_dir() / path, ec) && !args.value("overwrite", false)) {
          return std::unexpected(fail("CONFLICT", path + " already exists", "/clip", "Pass overwrite=true to replace it", path));
        }
        const AnimationClip clip = editor::build_clip(draft, sheet->width, sheet->height);
        const ClipFile previous = snapshot(context, path);
        if (auto ok = write_clip(context, path, clip); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"clip", path}, {"frames", clip.frames.size()}, {"cells", draft.cells}}, restore_call(previous)};
      });

  registry.add(
      {"clip.add_frames",
       "Insert grid cells `from`..`to` (inclusive, either order) into a clip at frame `index` (default: append). The clip is "
       "reopened on the given grid first; new frames last 1/fps of the first frame. Writes the file at once.",
       schema(sheet_props({{"from", kCell}, {"to", kCell}, {"index", kIndex}}), {"clip", "sheet", "columns", "rows", "from", "to"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto opened = open_on_grid(context, args);
        if (!opened) return std::unexpected(opened.error());
        ClipDraft& d = opened->draft;
        if (auto ok = check_range(d.grid, args); !ok) return std::unexpected(ok.error());
        const std::size_t index = args.contains("index") ? args["index"].get<std::size_t>() : d.cells.size();
        if (index > d.cells.size()) {
          return std::unexpected(fail("VALUE_INVALID", "index " + std::to_string(index) + " is above the frame count " +
                                                           std::to_string(d.cells.size()),
                                      "/index"));
        }
        ClipDraft added;
        added.add_range(args["from"], args["to"]);
        d.durations.resize(d.cells.size(), 0.0f);
        d.cells.insert(d.cells.begin() + static_cast<std::ptrdiff_t>(index), added.cells.begin(), added.cells.end());
        d.durations.insert(d.durations.begin() + static_cast<std::ptrdiff_t>(index), added.cells.size(), 0.0f);
        return commit(context, *opened, {{"added", added.cells.size()}, {"index", index}});
      });

  registry.add(
      {"clip.remove_frame",
       "Remove frame `index` from a clip (reopened on the given grid). Writes the file at once.",
       schema(sheet_props({{"index", kIndex}}), {"clip", "sheet", "columns", "rows", "index"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto opened = open_on_grid(context, args);
        if (!opened) return std::unexpected(opened.error());
        const std::size_t index = args["index"];
        if (index >= opened->draft.cells.size()) {
          return std::unexpected(fail("VALUE_INVALID", "frame " + std::to_string(index) + " does not exist (clip has " +
                                                           std::to_string(opened->draft.cells.size()) + ")",
                                      "/index"));
        }
        opened->draft.remove_frame(index);
        return commit(context, *opened, {{"index", index}});
      });

  registry.add(
      {"clip.move_frame",
       "Move frame `from` to position `to` (both 0-based frame indices; durations travel with the frame). Writes the file at once.",
       schema(sheet_props({{"from", kIndex}, {"to", kIndex}}), {"clip", "sheet", "columns", "rows", "from", "to"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto opened = open_on_grid(context, args);
        if (!opened) return std::unexpected(opened.error());
        const std::size_t count = opened->draft.cells.size();
        for (const char* key : {"from", "to"}) {
          if (args[key].get<std::size_t>() >= count) {
            return std::unexpected(fail("VALUE_INVALID", std::string("frame ") + std::to_string(args[key].get<std::size_t>()) +
                                                             " does not exist (clip has " + std::to_string(count) + ")",
                                        std::string("/") + key));
          }
        }
        opened->draft.move_frame(args["from"], args["to"]);
        return commit(context, *opened, {{"from", args["from"]}, {"to", args["to"]}});
      });

  registry.add(
      {"clip.set_frame",
       "Change frame `index` of a clip (reopened on the given grid): `cell` picks another grid cell, `seconds` (> 0) sets its "
       "duration. Writes the file at once.",
       schema(sheet_props({{"index", kIndex}, {"cell", kCell}, {"seconds", kSeconds}}), {"clip", "sheet", "columns", "rows", "index"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        if (!args.contains("cell") && !args.contains("seconds")) {
          return std::unexpected(fail("ARGS_INVALID", "give cell and/or seconds", "/cell"));
        }
        auto opened = open_on_grid(context, args);
        if (!opened) return std::unexpected(opened.error());
        ClipDraft& d = opened->draft;
        const std::size_t index = args["index"];
        if (index >= d.cells.size()) {
          return std::unexpected(fail("VALUE_INVALID", "frame " + std::to_string(index) + " does not exist (clip has " +
                                                           std::to_string(d.cells.size()) + ")",
                                      "/index"));
        }
        if (args.contains("cell")) {
          if (auto ok = check_cell(d.grid, args["cell"], "/cell"); !ok) return std::unexpected(ok.error());
          d.cells[index] = args["cell"];
        }
        if (args.contains("seconds")) {
          const float seconds = args["seconds"];
          if (!(seconds > 0.0f)) return std::unexpected(fail("ARGS_INVALID", "seconds must be above 0", "/seconds"));
          d.durations.resize(d.cells.size(), 0.0f);
          d.durations[index] = seconds;
        }
        return commit(context, *opened, {{"index", index}});
      });

  registry.add(
      {"clip.set",
       "Set the `name` and/or `loop` flag of a clip file; frames are kept exactly (no grid needed). Writes the file at once.",
       schema({{"clip", kString}, {"name", kString}, {"loop", kBool}}, {"clip"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        if (!args.contains("name") && !args.contains("loop")) {
          return std::unexpected(fail("ARGS_INVALID", "give name and/or loop", "/name"));
        }
        const std::string path = args["clip"];
        auto clip = load_clip(context, path);
        if (!clip) return std::unexpected(clip.error());
        const std::string before = save_clip_to_json(*clip);
        if (args.contains("name")) clip->name = args["name"];
        if (args.contains("loop")) clip->loop = args["loop"];
        Json value = {{"clip", path}, {"name", clip->name}, {"loop", clip->loop}};
        if (save_clip_to_json(*clip) == before) {
          value["changed"] = false;
          return CommandResult{std::move(value), std::nullopt};
        }
        const ClipFile previous = snapshot(context, path);
        if (auto ok = write_clip(context, path, *clip); !ok) return std::unexpected(ok.error());
        value["changed"] = true;
        return CommandResult{std::move(value), restore_call(previous)};
      });

  registry.add(
      {"clip.restore",
       "Put a clip file back to an earlier state: write `content` (the exact previous file text) or, with absent=true, delete "
       "the file. Used as the inverse of the other clip.* writes; not meant for authoring.",
       schema({{"clip", kString}, {"content", kString}, {"absent", kBool}}, {"clip"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string path = args["clip"];
        if (auto ok = check_path(path, "clip"); !ok) return std::unexpected(ok.error());
        const bool absent = args.value("absent", false);
        if (absent == args.contains("content")) {
          return std::unexpected(fail("ARGS_INVALID", "give either content or absent=true", "/content"));
        }
        const ClipFile previous = snapshot(context, path);
        const auto target = context.project_dir() / path;
        std::error_code ec;
        if (absent) {
          fs::remove(target, ec);
          if (ec) return std::unexpected(fail("IO_ERROR", "cannot remove " + path + ": " + ec.message(), "", {}, path));
        } else {
          fs::create_directories(target.parent_path(), ec);
          if (auto ok = write_text_file_atomic(target, args["content"].get<std::string>()); !ok) {
            return std::unexpected(fail("IO_ERROR", ok.error(), "", {}, path));
          }
        }
        return CommandResult{{{"clip", path}, {"restored", absent ? "absent" : "content"}}, restore_call(previous)};
      });
}

}  // namespace arcane::tooling
