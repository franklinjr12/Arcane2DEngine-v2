#include "arcane_editor/clip_authoring.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>

namespace arcane::editor {

UvRect grid_cell_uv(const SheetGrid& grid, int sheet_width, int sheet_height, int index) noexcept {
  if (grid.columns <= 0 || grid.rows <= 0 || sheet_width <= 0 || sheet_height <= 0 || index < 0 ||
      index >= grid.columns * grid.rows) {
    return UvRect{0.0f, 0.0f, 0.0f, 0.0f};
  }
  const int cell_w = grid.frame_width > 0 ? grid.frame_width : sheet_width / grid.columns;
  const int cell_h = grid.frame_height > 0 ? grid.frame_height : sheet_height / grid.rows;
  const int col = index % grid.columns;
  const int row = index / grid.columns;
  const int x = grid.margin + col * (cell_w + grid.spacing);
  const int y = grid.margin + row * (cell_h + grid.spacing);
  const float w = static_cast<float>(sheet_width);
  const float h = static_cast<float>(sheet_height);
  const auto f = [](int v) { return static_cast<float>(v); };
  return UvRect{f(x) / w, f(y) / h, f(x + cell_w) / w, f(y + cell_h) / h};
}

void ClipDraft::add_range(int first, int last) {
  const int step = first <= last ? 1 : -1;
  for (int i = first;; i += step) {
    cells.push_back(i);
    if (i == last) break;
  }
}

void ClipDraft::move_frame(std::size_t from, std::size_t to) {
  if (from >= cells.size() || to >= cells.size() || from == to) return;
  durations.resize(cells.size(), 0.0f);
  const int cell = cells[from];
  const float duration = durations[from];
  cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(from));
  durations.erase(durations.begin() + static_cast<std::ptrdiff_t>(from));
  cells.insert(cells.begin() + static_cast<std::ptrdiff_t>(to), cell);
  durations.insert(durations.begin() + static_cast<std::ptrdiff_t>(to), duration);
}

void ClipDraft::remove_frame(std::size_t index) {
  if (index >= cells.size()) return;
  durations.resize(cells.size(), 0.0f);
  cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(index));
  durations.erase(durations.begin() + static_cast<std::ptrdiff_t>(index));
}

AnimationClip build_clip(const ClipDraft& draft, int sheet_width, int sheet_height) {
  AnimationClip clip;
  clip.name = draft.name;
  clip.loop = draft.loop;
  const float default_duration = draft.fps > 0.0f ? 1.0f / draft.fps : 0.1f;
  for (std::size_t i = 0; i < draft.cells.size(); ++i) {
    const UvRect uv = grid_cell_uv(draft.grid, sheet_width, sheet_height, draft.cells[i]);
    if (uv.u1 <= uv.u0) continue;  // invalid cell index: skip rather than emit an empty frame
    const float custom = i < draft.durations.size() ? draft.durations[i] : 0.0f;
    clip.frames.push_back({.uv = uv, .duration_seconds = custom > 0.0f ? custom : default_duration});
  }
  return clip;
}

std::expected<ClipDraft, std::string> draft_from_clip(const AnimationClip& clip, const SheetGrid& grid,
                                                      int sheet_width, int sheet_height) {
  if (grid.columns <= 0 || grid.rows <= 0 || sheet_width <= 0 || sheet_height <= 0) {
    return std::unexpected("invalid grid or sheet size");
  }
  ClipDraft draft;
  draft.name = clip.name;
  draft.grid = grid;
  draft.loop = clip.loop;
  const float tol_u = 0.5f / static_cast<float>(sheet_width);
  const float tol_v = 0.5f / static_cast<float>(sheet_height);
  const int cell_count = grid.columns * grid.rows;
  for (std::size_t i = 0; i < clip.frames.size(); ++i) {
    const AnimationFrame& frame = clip.frames[i];
    if (!(frame.duration_seconds > 0.0f)) {
      return std::unexpected("frame " + std::to_string(i) + " has a non-positive duration");
    }
    int match = -1;
    for (int cell = 0; cell < cell_count && match < 0; ++cell) {
      const UvRect uv = grid_cell_uv(grid, sheet_width, sheet_height, cell);
      if (std::abs(uv.u0 - frame.uv.u0) <= tol_u && std::abs(uv.u1 - frame.uv.u1) <= tol_u &&
          std::abs(uv.v0 - frame.uv.v0) <= tol_v && std::abs(uv.v1 - frame.uv.v1) <= tol_v) {
        match = cell;
      }
    }
    if (match < 0) return std::unexpected("frame " + std::to_string(i) + " does not match a grid cell");
    draft.cells.push_back(match);
    draft.durations.push_back(frame.duration_seconds);
  }
  if (!draft.durations.empty()) draft.fps = 1.0f / draft.durations.front();
  return draft;
}

std::expected<void, std::string> save_clip_file(const std::filesystem::path& path, const AnimationClip& clip) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return std::unexpected("cannot open for writing: " + path.string());
  out << save_clip_to_json(clip);
  out.close();
  if (!out) return std::unexpected("write failed: " + path.string());
  return {};
}

std::expected<AnimationClip, std::string> load_clip_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::unexpected("cannot open: " + path.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return load_clip_from_json(ss.str());
}

std::filesystem::file_time_type file_stamp(const std::filesystem::path& path) {
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(path, ec);
  return ec ? std::filesystem::file_time_type::min() : t;
}

}  // namespace arcane::editor
