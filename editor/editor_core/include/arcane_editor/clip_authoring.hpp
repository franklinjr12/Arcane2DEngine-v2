#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "arcane/animation.hpp"

namespace arcane::editor {

// A sprite sheet laid out as a regular grid, cells numbered row-major from
// the top-left (cell 0 = first row, first column).
struct SheetGrid {
  int columns = 1;
  int rows = 1;
  int frame_width = 0;   // pixels; 0 = sheet_width / columns
  int frame_height = 0;  // pixels; 0 = sheet_height / rows
  int margin = 0;        // pixels around the whole grid
  int spacing = 0;       // pixels between cells
};

// UV rect (v down, matching Sprite::uv conventions) of cell `index`, or an
// empty rect if the grid or index is invalid.
UvRect grid_cell_uv(const SheetGrid& grid, int sheet_width, int sheet_height, int index) noexcept;

// An in-progress clip: which grid cells play, in order, and for how long.
struct ClipDraft {
  std::string name;
  SheetGrid grid;
  std::vector<int> cells;  // frame order; a cell may repeat
  float fps = 10.0f;       // used for frames without a custom duration
  std::vector<float> durations;  // optional per-frame override (seconds); 0/missing = 1/fps
  bool loop = true;

  // Appends cells [first, last] (inclusive, either order).
  void add_range(int first, int last);
  void move_frame(std::size_t from, std::size_t to);
  void remove_frame(std::size_t index);
};

AnimationClip build_clip(const ClipDraft& draft, int sheet_width, int sheet_height);

// Reopens an existing clip into a draft on `grid` (the caller supplies the grid; clip files do not store
// it). Every frame must be exactly a grid cell (within 1/2 texel) and have a positive duration, otherwise
// an error names the frame, so that build_clip(draft) reproduces the clip instead of silently altering it.
// Durations are kept per frame; `fps` is set to the first frame's rate for newly added frames.
std::expected<ClipDraft, std::string> draft_from_clip(const AnimationClip& clip, const SheetGrid& grid,
                                                      int sheet_width, int sheet_height);

std::expected<void, std::string> save_clip_file(const std::filesystem::path& path, const AnimationClip& clip);
std::expected<AnimationClip, std::string> load_clip_file(const std::filesystem::path& path);

// Last-write time used for hot-reload polling; nullopt-equivalent (min) if the file is missing.
std::filesystem::file_time_type file_stamp(const std::filesystem::path& path);

}  // namespace arcane::editor
