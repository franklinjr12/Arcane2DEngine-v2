// asset.list / asset.import / asset.reimport (docs/commands.md, "Assets"): file-level commands on <project>/assets.
// Asset ids are derived from the assets-relative path, so there is no import database; a write is an atomic
// rename, which running editors and players pick up by polling file times. Content checks look at headers only.
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "arcane/animation.hpp"
#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "offline_internal.hpp"

namespace arcane::tooling {

namespace {

using Result = std::expected<Json, ToolError>;
namespace fs = std::filesystem;

enum class Kind { Image, Clip, Audio };

const char* kind_name(Kind kind) {
  switch (kind) {
    case Kind::Image: return "image";
    case Kind::Clip: return "clip";
    case Kind::Audio: return "audio";
  }
  return "image";
}

std::optional<Kind> kind_of_extension(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") return Kind::Image;
  if (ext == ".json") return Kind::Clip;
  if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") return Kind::Audio;
  return std::nullopt;
}

bool starts_with(const std::string& bytes, std::string_view prefix) { return bytes.compare(0, prefix.size(), prefix) == 0; }

std::uint32_t be32(const std::string& bytes, std::size_t at) {
  std::uint32_t v = 0;
  for (std::size_t i = 0; i < 4; ++i) v = (v << 8) | static_cast<unsigned char>(bytes[at + i]);
  return v;
}

// Header-only content check. Returns the problem, or an empty string.
std::string content_problem(Kind kind, const fs::path& name, const std::string& bytes) {
  const std::string ext = [&] {
    std::string e = name.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
  }();
  switch (kind) {
    case Kind::Image:
      if (ext == ".png") {
        if (bytes.size() < 24 || !starts_with(bytes, std::string_view("\x89PNG\r\n\x1a\n", 8)) || bytes.compare(12, 4, "IHDR") != 0) {
          return "not a PNG file (missing signature or IHDR)";
        }
        if (be32(bytes, 16) == 0 || be32(bytes, 20) == 0) return "PNG has a zero width or height";
        return {};
      }
      if (ext == ".bmp") return starts_with(bytes, "BM") ? "" : "not a BMP file (missing 'BM')";
      return starts_with(bytes, std::string_view("\xFF\xD8\xFF", 3)) ? "" : "not a JPEG file (missing FF D8 FF)";
    case Kind::Audio: {
      const bool wav = bytes.size() >= 12 && starts_with(bytes, "RIFF") && bytes.compare(8, 4, "WAVE") == 0;
      const bool mp3 = starts_with(bytes, "ID3") || (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
                                                      (static_cast<unsigned char>(bytes[1]) & 0xE0) == 0xE0);
      if (wav || starts_with(bytes, "OggS") || starts_with(bytes, "fLaC") || mp3) return {};
      return "not a WAV, Ogg, FLAC or MP3 file (unknown magic bytes)";
    }
    case Kind::Clip: {
      auto clip = load_clip_from_json(bytes);
      return clip ? std::string() : clip.error();
    }
  }
  return {};
}

std::expected<std::string, ToolError> read_binary(const fs::path& path, const std::string& shown) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return std::unexpected(offline_error("IO_ERROR", "cannot read '" + shown + "': not a file", shown));
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::unexpected(offline_error("IO_ERROR", "cannot open '" + shown + "'", shown));
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad()) return std::unexpected(offline_error("IO_ERROR", "cannot read '" + shown + "'", shown));
  return ss.str();
}

ToolError invalid_content(const std::string& source, const std::string& problem) {
  return offline_error("VALUE_INVALID", "'" + source + "' is not a valid asset: " + problem, source, "/source",
                       "Nothing was written; fix or replace the source file");
}

Result asset_list(const OfflineEnv& env, const Json& args, bool) {
  if (auto ok = offline_require_project(env); !ok) return std::unexpected(ok.error());
  const std::string wanted = args.value("kind", "all");
  const fs::path assets = env.project_dir / "assets";
  struct Row {
    editor::AssetEntry entry;
    Kind kind;
  };
  std::vector<Row> rows;
  auto add = [&](Kind kind, std::vector<editor::AssetEntry> entries) {
    if (wanted != "all" && wanted != kind_name(kind)) return;
    for (auto& e : entries) rows.push_back({std::move(e), kind});
  };
  add(Kind::Image, editor::list_image_assets(assets));
  add(Kind::Clip, editor::list_animation_clip_assets(assets));
  add(Kind::Audio, editor::list_audio_assets(assets));
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.entry.relative_path < b.entry.relative_path; });
  Json out = Json::array();
  for (const Row& row : rows) {
    std::error_code ec;
    const auto size = fs::file_size(row.entry.absolute_path, ec);
    out.push_back({{"id", row.entry.id}, {"path", row.entry.relative_path}, {"kind", kind_name(row.kind)}, {"bytes", ec ? 0 : size}});
  }
  return Json{{"assets", out}};
}

// Shared by import and reimport: a checked source ready to be written to `dest` (relative to assets/).
struct Checked {
  Kind kind;
  std::string bytes;
};

std::expected<Checked, ToolError> check_source(const std::string& source, Kind kind) {
  auto bytes = read_binary(source, source);
  if (!bytes) return std::unexpected(bytes.error());
  if (auto problem = content_problem(kind, fs::path(source), *bytes); !problem.empty()) return std::unexpected(invalid_content(source, problem));
  return Checked{kind, std::move(*bytes)};
}

std::expected<Kind, ToolError> kind_for(const std::string& path, const char* arg) {
  if (auto kind = kind_of_extension(path)) return *kind;
  return std::unexpected(offline_error("ARGS_INVALID", "'" + path + "' has no supported asset extension", "", std::string("/") + arg,
                                       "Supported: .png .jpg .jpeg .bmp (image), .json (clip), .wav .ogg .mp3 .flac (audio)"));
}

std::expected<void, ToolError> check_dest(const std::string& dest, const char* arg) {
  if (is_project_relative_path(dest)) return {};
  return std::unexpected(offline_error("ARGS_INVALID", "'" + dest + "' is not a relative '/' path inside assets/", "", std::string("/") + arg,
                                       "Use a path like sprites/hero.png (no '..', no absolute or drive paths, no backslashes)"));
}

Result asset_import(const OfflineEnv& env, const Json& args, bool dry_run) {
  if (auto ok = offline_require_project(env); !ok) return std::unexpected(ok.error());
  const std::string source = args["source"].get<std::string>();
  const std::string dest = args.contains("dest") ? args["dest"].get<std::string>() : fs::path(source).filename().generic_string();
  if (auto ok = check_dest(dest, "dest"); !ok) return std::unexpected(ok.error());
  auto kind = kind_for(dest, "dest");
  if (!kind) return std::unexpected(kind.error());
  auto source_kind = kind_for(source, "source");
  if (!source_kind) return std::unexpected(source_kind.error());
  if (*source_kind != *kind) {
    return std::unexpected(offline_error("ARGS_INVALID", "source is a " + std::string(kind_name(*source_kind)) + " but dest is a " + kind_name(*kind),
                                         "", "/dest", "Give dest the same kind of extension as the source"));
  }
  auto checked = check_source(source, *kind);
  if (!checked) return std::unexpected(checked.error());

  const fs::path target = env.project_dir / "assets" / dest;
  std::error_code ec;
  const bool exists = fs::exists(target, ec);
  if (exists && !args.value("overwrite", false)) {
    return std::unexpected(offline_error("CONFLICT", "assets/" + dest + " already exists", "assets/" + dest, "/dest",
                                         "Pass overwrite=true to replace it (asset.reimport keeps the same id)"));
  }
  if (exists && !fs::is_regular_file(target, ec)) {
    return std::unexpected(offline_error("CONFLICT", "assets/" + dest + " exists and is not a file", "assets/" + dest, "/dest"));
  }
  if (!dry_run) {
    fs::create_directories(target.parent_path(), ec);
    if (auto written = write_text_file_atomic(target, checked->bytes); !written) {
      return std::unexpected(offline_error("IO_ERROR", written.error(), "assets/" + dest));
    }
  }
  return Json{{"id", editor::asset_id_for_relative_path(dest)}, {"path", dest}, {"kind", kind_name(*kind)},
              {"bytes", checked->bytes.size()}, {"replaced", exists}};
}

Result asset_reimport(const OfflineEnv& env, const Json& args, bool dry_run) {
  if (auto ok = offline_require_project(env); !ok) return std::unexpected(ok.error());
  const std::string path = args["path"].get<std::string>();
  if (auto ok = check_dest(path, "path"); !ok) return std::unexpected(ok.error());
  auto kind = kind_for(path, "path");
  if (!kind) return std::unexpected(kind.error());
  const fs::path target = env.project_dir / "assets" / path;
  auto current = read_binary(target, "assets/" + path);
  if (!current) return std::unexpected(current.error());

  Json result = {{"id", editor::asset_id_for_relative_path(path)}, {"path", path}, {"kind", kind_name(*kind)}};
  if (!args.contains("source")) {
    if (auto problem = content_problem(*kind, fs::path(path), *current); !problem.empty()) {
      return std::unexpected(offline_error("VALUE_INVALID", "assets/" + path + " is not a valid asset: " + problem, "assets/" + path, "/path",
                                           "The file was not modified; replace it with asset.reimport --source"));
    }
    result["bytes"] = current->size();
    result["changed"] = false;
    return result;
  }
  const std::string source = args["source"].get<std::string>();
  auto source_kind = kind_for(source, "source");
  if (!source_kind) return std::unexpected(source_kind.error());
  if (*source_kind != *kind) {
    return std::unexpected(offline_error("ARGS_INVALID", "source is a " + std::string(kind_name(*source_kind)) + " but " + path + " is a " + kind_name(*kind),
                                         "", "/source", "Reimport with a source of the same kind"));
  }
  auto checked = check_source(source, *kind);
  if (!checked) return std::unexpected(checked.error());
  const bool changed = checked->bytes != *current;
  if (changed && !dry_run) {
    if (auto written = write_text_file_atomic(target, checked->bytes); !written) {
      return std::unexpected(offline_error("IO_ERROR", written.error(), "assets/" + path));
    }
  }
  result["bytes"] = checked->bytes.size();
  result["changed"] = changed;
  return result;
}

}  // namespace

std::vector<OfflineCommand> make_asset_commands() {
  const Json kStr = {{"type", "string"}};
  const Json kBool = {{"type", "boolean"}};
  std::vector<OfflineCommand> c;
  c.push_back({{"asset.list", "List the image, clip and audio files under assets/ with their path-derived ids, sorted by path.",
                offline_object_schema({{"kind", {{"type", "string"}, {"enum", Json::array({"image", "clip", "audio", "all"})}}}}), {{"type", "object"}}, false},
               asset_list});
  c.push_back({{"asset.import", "Copy a checked image (.png/.jpg/.jpeg/.bmp), clip (.json) or audio (.wav/.ogg/.mp3/.flac) file into assets/; "
                                "never overwrites unless overwrite is set. Content is checked by header only.",
                offline_object_schema({{"source", kStr}, {"dest", kStr}, {"overwrite", kBool}}, {"source"}, {"source", "dest"}), {{"type", "object"}}, true},
               asset_import});
  c.push_back({{"asset.reimport", "Re-validate an existing asset in place, or atomically replace it from a checked source of the same kind; the id does not change.",
                offline_object_schema({{"path", kStr}, {"source", kStr}}, {"path"}, {"path"}), {{"type", "object"}}, true},
               asset_reimport});
  return c;
}

}  // namespace arcane::tooling
