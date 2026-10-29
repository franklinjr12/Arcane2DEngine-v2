// atlas.* commands (docs/formats/atlas.md, docs/commands.md). They read and
// write asset files only; the open scene is never touched, so they ignore
// `persist`/`dry_run` like prefab.save.
#include <filesystem>

#include "arcane/animation.hpp"
#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

ToolError args_error(std::string message, std::string path) {
  return ToolError{"ARGS_INVALID", std::move(message), "", std::move(path), ""};
}

std::expected<void, ToolError> check_path(const std::string& value, const char* arg) {
  if (is_project_relative_path(value)) return {};
  return std::unexpected(args_error("'" + value + "' is not a project-relative '/' path", std::string("/") + arg));
}

std::expected<Atlas, ToolError> load_atlas(ProjectContext& context, const std::string& file) {
  if (auto ok = check_path(file, "atlas"); !ok) return std::unexpected(ok.error());
  auto text = read_text_file(context.project_dir() / file);
  if (!text) return std::unexpected(ToolError{"IO_ERROR", text.error(), file, "", "Create it with atlas.pack"});
  auto atlas = load_atlas_from_json(*text);
  if (!atlas) return std::unexpected(ToolError{"SCHEMA_INVALID", atlas.error(), file, "", ""});
  return std::move(*atlas);
}

Json uv_json(const UvRect& uv) { return Json::array({uv.u0, uv.v0, uv.u1, uv.v1}); }

Json atlas_json(const Atlas& atlas) {
  Json regions = Json::array();
  for (const auto& r : atlas.regions) {
    regions.push_back({{"name", r.name}, {"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h},
                       {"uv", uv_json(atlas_region_uv(atlas, r))}});
  }
  return {{"image", atlas.image}, {"width", atlas.width}, {"height", atlas.height},
          {"padding", atlas.padding}, {"regions", regions}};
}

}  // namespace

void register_atlas_commands(CommandRegistry& registry) {
  registry.add(
      {"atlas.pack",
       "Pack every PNG under a project folder into one atlas image plus canonical atlas metadata. Deterministic: the "
       "same inputs give the same bytes. Writes both files at once.",
       {{"type", "object"},
        {"properties",
         {{"folder", {{"type", "string"}}},
          {"atlas", {{"type", "string"}}},
          {"image", {{"type", "string"}}},
          {"padding", {{"type", "integer"}, {"minimum", 0}, {"maximum", 64}}},
          {"max_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16384}}}}},
        {"required", {"folder", "atlas"}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string folder = args["folder"];
        const std::string file = args["atlas"];
        if (auto ok = check_path(folder, "folder"); !ok) return std::unexpected(ok.error());
        if (auto ok = check_path(file, "atlas"); !ok) return std::unexpected(ok.error());
        std::string image = args.value("image", std::string());
        if (image.empty()) {
          image = file.size() > 5 && file.ends_with(".json") ? file.substr(0, file.size() - 5) + ".png" : file + ".png";
        }
        if (auto ok = check_path(image, "image"); !ok) return std::unexpected(ok.error());
        auto atlas = pack_atlas_folder(context.project_dir(), folder, image, file, args.value("padding", 1),
                                       args.value("max_size", 2048));
        if (!atlas) {
          const bool io = atlas.error().find("cannot write") != std::string::npos ||
                          atlas.error().find("not found") != std::string::npos;
          return std::unexpected(ToolError{io ? "IO_ERROR" : "VALUE_INVALID", atlas.error(), file, "", ""});
        }
        Json value = atlas_json(*atlas);
        value["atlas"] = file;
        return CommandResult{std::move(value), std::nullopt};
      });

  registry.add(
      {"atlas.regions",
       "List the regions of an atlas file with pixel rectangles and UV rects [u0,v0,u1,v1]; with `name`, only that region.",
       {{"type", "object"},
        {"properties", {{"atlas", {{"type", "string"}}}, {"name", {{"type", "string"}}}}},
        {"required", {"atlas"}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto atlas = load_atlas(context, args["atlas"]);
        if (!atlas) return std::unexpected(atlas.error());
        Json value = atlas_json(*atlas);
        if (args.contains("name")) {
          const std::string name = args["name"];
          Json one = Json::array();
          for (const auto& r : value["regions"]) {
            if (r["name"] == name) one.push_back(r);
          }
          if (one.empty()) {
            return std::unexpected(ToolError{"VALUE_INVALID", "no region '" + name + "' in " + std::string(args["atlas"]),
                                             args["atlas"], "/name", "List regions with atlas.regions"});
          }
          value["regions"] = one;
        }
        return CommandResult{std::move(value), std::nullopt};
      });

  registry.add(
      {"atlas.make_clip",
       "Write an animation clip file whose frames are atlas regions (in the given order). The sprite using the clip "
       "still points its texture at the atlas image.",
       {{"type", "object"},
        {"properties",
         {{"atlas", {{"type", "string"}}},
          {"regions", {{"type", "array"}}},
          {"clip", {{"type", "string"}}},
          {"name", {{"type", "string"}}},
          {"frame_seconds", {{"type", "number"}, {"minimum", 0}}},
          {"loop", {{"type", "boolean"}}},
          {"overwrite", {{"type", "boolean"}}}}},
        {"required", {"atlas", "regions", "clip"}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string clip_file = args["clip"];
        if (auto ok = check_path(clip_file, "clip"); !ok) return std::unexpected(ok.error());
        auto atlas = load_atlas(context, args["atlas"]);
        if (!atlas) return std::unexpected(atlas.error());
        if (args["regions"].empty()) return std::unexpected(args_error("regions must not be empty", "/regions"));
        AnimationClip clip;
        clip.name = args.value("name", std::string());
        clip.loop = args.value("loop", true);
        const float seconds = args.value("frame_seconds", 0.1f);
        for (std::size_t i = 0; i < args["regions"].size(); ++i) {
          const Json& entry = args["regions"][i];
          const std::string where = "/regions/" + std::to_string(i);
          if (!entry.is_string()) return std::unexpected(args_error("region names are strings", where));
          const AtlasRegion* region = find_atlas_region(*atlas, entry.get<std::string>());
          if (!region) {
            return std::unexpected(ToolError{"VALUE_INVALID", "no region '" + entry.get<std::string>() + "' in " +
                                                                 std::string(args["atlas"]),
                                             args["atlas"], where, "List regions with atlas.regions"});
          }
          clip.frames.push_back({atlas_region_uv(*atlas, *region), seconds});
        }
        const auto target = context.project_dir() / clip_file;
        std::error_code ec;
        if (std::filesystem::exists(target, ec) && !args.value("overwrite", false)) {
          return std::unexpected(ToolError{"CONFLICT", clip_file + " already exists", clip_file, "/clip",
                                           "Pass overwrite=true to replace it"});
        }
        auto text = save_clip_canonical_json(clip);
        if (!text) return std::unexpected(ToolError{"VALUE_INVALID", text.error(), clip_file, "", ""});
        std::filesystem::create_directories(target.parent_path(), ec);
        if (auto ok = write_text_file_atomic(target, *text); !ok) {
          return std::unexpected(ToolError{"IO_ERROR", ok.error(), clip_file, "", ""});
        }
        return CommandResult{{{"clip", clip_file}, {"frames", clip.frames.size()}}, std::nullopt};
      });
}

}  // namespace arcane::tooling
