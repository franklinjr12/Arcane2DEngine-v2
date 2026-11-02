#include "arcane/atlas.hpp"

#include <json.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <set>

#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "arcane/canonical_json.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {

constexpr const char* kAtlasFormat = "arcane.atlas";

struct Placement {
  std::vector<int> x, y;  // by index into the sorted order
  int height = 0;
};

// Shelf-packs `order` into rows of at most `width` pixels; false if it does not fit.
bool shelf_pack(const std::vector<AtlasInput>& order, int padding, int width, int max_size, Placement& out) {
  out.x.assign(order.size(), 0);
  out.y.assign(order.size(), 0);
  int cx = padding, cy = padding, row_h = 0;
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (order[i].w + 2 * padding > width) return false;
    if (cx + order[i].w + padding > width) {
      cy += row_h + padding;
      cx = padding;
      row_h = 0;
    }
    out.x[i] = cx;
    out.y[i] = cy;
    cx += order[i].w + padding;
    row_h = std::max(row_h, order[i].h);
  }
  out.height = cy + row_h + padding;
  return out.height <= max_size;
}

}  // namespace

bool is_project_relative_path(std::string_view path) noexcept {
  if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos) return false;
  if (path.size() > 1 && path[1] == ':') return false;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    const std::string_view segment = path.substr(start, end - start);
    if (segment.empty() || segment == "." || segment == "..") return false;
    start = end + 1;
  }
  return true;
}

std::expected<Atlas, std::string> pack_atlas_layout(const std::vector<AtlasInput>& inputs, int padding, int max_size) {
  if (padding < 0 || max_size < 1) return std::unexpected(std::string("atlas: padding must be >= 0 and max_size >= 1"));
  if (inputs.empty()) return std::unexpected(std::string("atlas: no images to pack"));
  std::set<std::string> names;
  for (const auto& in : inputs) {
    if (in.w < 1 || in.h < 1) return std::unexpected(std::format("atlas: '{}' has an empty size", in.name));
    if (!names.insert(in.name).second) return std::unexpected(std::format("atlas: duplicate name '{}'", in.name));
  }
  std::vector<AtlasInput> order = inputs;
  std::sort(order.begin(), order.end(), [](const AtlasInput& a, const AtlasInput& b) {
    if (a.h != b.h) return a.h > b.h;
    if (a.w != b.w) return a.w > b.w;
    return a.name < b.name;
  });

  Placement best;
  int best_width = 0;
  bool found = false;
  for (int width = 1; width <= max_size && !found; width *= 2) {
    Placement p;
    if (!shelf_pack(order, padding, width, max_size, p)) continue;
    if (p.height <= width || width * 2 > max_size) {
      best = std::move(p);
      best_width = width;
      found = true;
    }
  }
  if (!found) {
    // Largest width that is still allowed (max_size need not be a power of two).
    Placement p;
    if (shelf_pack(order, padding, max_size, max_size, p)) {
      best = std::move(p);
      best_width = max_size;
      found = true;
    }
  }
  if (!found) return std::unexpected(std::format("atlas: images do not fit into {0}x{0} with padding {1}", max_size, padding));

  Atlas atlas;
  atlas.width = best_width;
  atlas.height = best.height;
  atlas.padding = padding;
  for (std::size_t i = 0; i < order.size(); ++i) {
    atlas.regions.push_back({order[i].name, best.x[i], best.y[i], order[i].w, order[i].h});
  }
  std::sort(atlas.regions.begin(), atlas.regions.end(),
            [](const AtlasRegion& a, const AtlasRegion& b) { return a.name < b.name; });
  return atlas;
}

namespace {

struct Decoded {
  std::string name;
  int w = 0, h = 0;
  std::vector<unsigned char> rgba;
};

bool has_png_extension(const std::filesystem::path& p) {
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".png";
}

void write_png_bytes(void* context, void* data, int size) {
  static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

}  // namespace

std::expected<Atlas, std::string> pack_atlas_folder(const std::filesystem::path& project_dir, const std::string& folder,
                                                    const std::string& image_out, const std::string& atlas_out,
                                                    int padding, int max_size) {
  namespace fs = std::filesystem;
  for (const auto* p : {&folder, &image_out, &atlas_out}) {
    if (!is_project_relative_path(*p)) return std::unexpected(std::format("atlas: '{}' is not a project-relative path", *p));
  }
  std::error_code ec;
  const fs::path root = project_dir / folder;
  if (!fs::is_directory(root, ec)) return std::unexpected(std::format("atlas: folder not found: {}", folder));
  const fs::path image_abs = (project_dir / image_out).lexically_normal();

  std::vector<std::string> files;  // relative to root, '/' separated
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file() || !has_png_extension(it->path())) continue;
    if (it->path().lexically_normal() == image_abs) continue;
    files.push_back(it->path().lexically_relative(root).generic_string());
  }
  if (ec) return std::unexpected(std::format("atlas: cannot read folder {}: {}", folder, ec.message()));
  std::sort(files.begin(), files.end());

  std::vector<Decoded> images;
  std::vector<AtlasInput> inputs;
  for (const auto& rel : files) {
    auto bytes = read_text_file(root / rel);
    if (!bytes) return std::unexpected(std::format("atlas: {}", bytes.error()));
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes->data()),
                                              static_cast<int>(bytes->size()), &w, &h, &comp, 4);
    if (!px) return std::unexpected(std::format("atlas: cannot decode {}: {}", rel, stbi_failure_reason()));
    Decoded d;
    d.name = rel.substr(0, rel.size() - 4);
    d.w = w;
    d.h = h;
    d.rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    inputs.push_back({d.name, w, h});
    images.push_back(std::move(d));
  }

  auto atlas = pack_atlas_layout(inputs, padding, max_size);
  if (!atlas) return std::unexpected(atlas.error());
  atlas->image = image_out;

  std::vector<unsigned char> canvas(static_cast<std::size_t>(atlas->width) * atlas->height * 4, 0);
  for (const auto& region : atlas->regions) {
    const auto img = std::find_if(images.begin(), images.end(), [&](const Decoded& d) { return d.name == region.name; });
    for (int row = 0; row < region.h; ++row) {
      std::memcpy(&canvas[(static_cast<std::size_t>(region.y + row) * atlas->width + region.x) * 4],
                  &img->rgba[static_cast<std::size_t>(row) * region.w * 4], static_cast<std::size_t>(region.w) * 4);
    }
  }
  std::string png;
  if (!stbi_write_png_to_func(write_png_bytes, &png, atlas->width, atlas->height, 4, canvas.data(), atlas->width * 4)) {
    return std::unexpected(std::string("atlas: PNG encoding failed"));
  }
  auto json = save_atlas_canonical_json(*atlas);
  if (!json) return std::unexpected(json.error());

  // Image first, then metadata, so a metadata file never points at a missing image.
  fs::create_directories(image_abs.parent_path(), ec);
  const fs::path tmp = image_abs.string() + ".arcane-tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    out.close();
    if (!out) return std::unexpected(std::format("atlas: cannot write {}", image_out));
  }
  fs::rename(tmp, image_abs, ec);
  if (ec) return std::unexpected(std::format("atlas: cannot write {}: {}", image_out, ec.message()));
  fs::create_directories((project_dir / atlas_out).parent_path(), ec);
  if (auto ok = write_text_file_atomic(project_dir / atlas_out, *json); !ok) {
    return std::unexpected(std::format("atlas: {}", ok.error()));
  }
  return atlas;
}

std::expected<std::string, std::string> save_atlas_canonical_json(const Atlas& atlas) {
  nlohmann::json j;
  j["format"] = kAtlasFormat;
  j["version"] = 2;
  j["image"] = atlas.image;
  j["width"] = atlas.width;
  j["height"] = atlas.height;
  j["padding"] = atlas.padding;
  nlohmann::json regions = nlohmann::json::array();
  for (const auto& r : atlas.regions) {
    regions.push_back({{"name", r.name}, {"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}});
  }
  j["regions"] = std::move(regions);
  return canonical::dump(j);
}

std::expected<Atlas, std::string> load_atlas_from_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("atlas: {}", parsed.error()));
  try {
    const nlohmann::json& j = *parsed;
    canonical::require_object(j, "atlas");
    if (canonical::detect_dialect(j, kAtlasFormat, "atlas") != canonical::Dialect::V2) {
      return std::unexpected(std::string("atlas: needs format 'arcane.atlas' version 2"));
    }
    canonical::require_only_keys(j, {"format", "version", "image", "width", "height", "padding", "regions"}, "atlas");
    for (const char* key : {"image", "width", "height", "regions"}) {
      if (!j.contains(key)) return std::unexpected(std::format("atlas: missing '{}'", key));
    }
    Atlas atlas;
    atlas.image = canonical::as_string(j["image"], "atlas 'image'");
    canonical::require_project_path(atlas.image, false, "atlas 'image'");
    atlas.width = canonical::as_int(j["width"], "atlas 'width'");
    atlas.height = canonical::as_int(j["height"], "atlas 'height'");
    if (j.contains("padding")) atlas.padding = canonical::as_int(j["padding"], "atlas 'padding'");
    if (atlas.width < 1 || atlas.height < 1 || atlas.padding < 0) {
      return std::unexpected(std::string("atlas: width/height must be >= 1 and padding >= 0"));
    }
    if (!j["regions"].is_array()) return std::unexpected(std::string("atlas: 'regions' must be an array"));
    std::set<std::string> names;
    for (std::size_t i = 0; i < j["regions"].size(); ++i) {
      const auto& r = j["regions"][i];
      const std::string where = std::format("atlas region {}", i);
      canonical::require_object(r, where);
      canonical::require_only_keys(r, {"name", "x", "y", "w", "h"}, where);
      for (const char* key : {"name", "x", "y", "w", "h"}) {
        if (!r.contains(key)) return std::unexpected(std::format("atlas: {} is missing '{}'", where, key));
      }
      AtlasRegion region{canonical::as_string(r["name"], where), canonical::as_int(r["x"], where),
                         canonical::as_int(r["y"], where), canonical::as_int(r["w"], where),
                         canonical::as_int(r["h"], where)};
      if (region.name.empty() || !names.insert(region.name).second) {
        return std::unexpected(std::format("atlas: {} has an empty or duplicate name", where));
      }
      if (region.x < 0 || region.y < 0 || region.w < 1 || region.h < 1 || region.x + region.w > atlas.width ||
          region.y + region.h > atlas.height) {
        return std::unexpected(std::format("atlas: region '{}' is outside the {}x{} image", region.name, atlas.width, atlas.height));
      }
      atlas.regions.push_back(std::move(region));
    }
    return atlas;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::format("atlas: {}", ex.what()));
  }
}

const AtlasRegion* find_atlas_region(const Atlas& atlas, std::string_view name) noexcept {
  for (const auto& r : atlas.regions) {
    if (r.name == name) return &r;
  }
  return nullptr;
}

UvRect atlas_region_uv(const Atlas& atlas, const AtlasRegion& r) noexcept {
  const float w = static_cast<float>(atlas.width), h = static_cast<float>(atlas.height);
  return UvRect{r.x / w, r.y / h, (r.x + r.w) / w, (r.y + r.h) / h};
}

}  // namespace arcane
