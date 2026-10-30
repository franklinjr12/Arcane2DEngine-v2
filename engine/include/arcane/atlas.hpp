#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "arcane/export.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

// Pixel rectangle of one source image inside an atlas (origin top-left).
struct AtlasRegion {
  std::string name;  // source path relative to the packed folder, without extension ("hero/idle_0")
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

// Canonical `arcane.atlas` v2 (docs/formats/atlas.md): where the packed image
// lives and where every source image went. UV rects are derived, never stored.
struct Atlas {
  std::string image;  // project-relative path of the atlas PNG
  int width = 0;
  int height = 0;
  int padding = 0;  // empty pixels between regions and around the edge
  std::vector<AtlasRegion> regions;  // sorted by name
};

struct AtlasInput {
  std::string name;
  int w = 0;
  int h = 0;
};

// Deterministic shelf packing: inputs are ordered by height, width, then name;
// the atlas width is the smallest power of two (up to max_size) whose result is
// not taller than wide, otherwise the smallest height that fits. Returns the
// regions sorted by name. Fails on duplicate names, empty sizes, or when
// nothing fits into max_size x max_size.
ARCANE_API std::expected<Atlas, std::string> pack_atlas_layout(const std::vector<AtlasInput>& inputs, int padding,
                                                               int max_size);

// Packs every *.png under project_dir/folder (recursive, sorted by path) into
// project_dir/image_out and writes the metadata to project_dir/atlas_out.
// Output files inside the folder are ignored as inputs. Same input bytes give
// the same output bytes.
ARCANE_API std::expected<Atlas, std::string> pack_atlas_folder(const std::filesystem::path& project_dir,
                                                               const std::string& folder,
                                                               const std::string& image_out,
                                                               const std::string& atlas_out, int padding,
                                                               int max_size);

ARCANE_API std::expected<std::string, std::string> save_atlas_canonical_json(const Atlas& atlas);
ARCANE_API std::expected<Atlas, std::string> load_atlas_from_json(const std::string& text);

// nullptr if the atlas has no region of that name.
ARCANE_API const AtlasRegion* find_atlas_region(const Atlas& atlas, std::string_view name) noexcept;
// Texture coordinates of a region (u0,v0 = top-left, u1,v1 = bottom-right).
ARCANE_API UvRect atlas_region_uv(const Atlas& atlas, const AtlasRegion& region) noexcept;

// Forward-slash project-relative path check shared with the commands.
ARCANE_API bool is_project_relative_path(std::string_view path) noexcept;

}  // namespace arcane
