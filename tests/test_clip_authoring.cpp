#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "arcane_editor/clip_authoring.hpp"
#include "arcane_editor/scene_render.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
using namespace arcane::editor;

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

void test_grid_slicing() {
  SheetGrid grid{.columns = 4, .rows = 2};
  const UvRect first = grid_cell_uv(grid, 128, 64, 0);
  ARCANE_ASSERT_TRUE(near(first.u0, 0) && near(first.v0, 0) && near(first.u1, 0.25f) && near(first.v1, 0.5f));
  const UvRect last = grid_cell_uv(grid, 128, 64, 7);
  ARCANE_ASSERT_TRUE(near(last.u0, 0.75f) && near(last.v0, 0.5f) && near(last.u1, 1) && near(last.v1, 1));
  // Explicit frame size with margin and spacing: cell 1 starts at x = 2 + 16 + 1.
  SheetGrid padded{.columns = 2, .rows = 1, .frame_width = 16, .frame_height = 16, .margin = 2, .spacing = 1};
  const UvRect second = grid_cell_uv(padded, 100, 50, 1);
  ARCANE_ASSERT_TRUE(near(second.u0, 19 / 100.0f) && near(second.u1, 35 / 100.0f) && near(second.v0, 2 / 50.0f));
  // Out-of-range / invalid input gives an empty rect.
  ARCANE_ASSERT_TRUE(grid_cell_uv(grid, 128, 64, 8).u1 == 0.0f);
  ARCANE_ASSERT_TRUE(grid_cell_uv(grid, 128, 64, -1).u1 == 0.0f);
  ARCANE_ASSERT_TRUE(grid_cell_uv(grid, 0, 64, 0).u1 == 0.0f);
}

void test_draft_editing_and_build() {
  ClipDraft draft;
  draft.name = "run";
  draft.grid = {.columns = 4, .rows = 1};
  draft.fps = 10.0f;
  draft.add_range(0, 2);
  draft.add_range(3, 3);
  ARCANE_ASSERT_TRUE(draft.cells.size() == 4);
  draft.move_frame(3, 0);  // 3,0,1,2
  ARCANE_ASSERT_TRUE(draft.cells[0] == 3 && draft.cells[3] == 2);
  draft.durations = {0.5f, 0, 0, 0};
  draft.move_frame(0, 1);  // 0,3,1,2 -- duration travels with the frame
  ARCANE_ASSERT_TRUE(draft.durations[1] == 0.5f);
  draft.remove_frame(0);
  draft.cells.push_back(99);  // invalid cell is skipped by build_clip
  const AnimationClip clip = build_clip(draft, 64, 16);
  ARCANE_ASSERT_TRUE(clip.frames.size() == 3);
  ARCANE_ASSERT_TRUE(near(clip.frames[0].duration_seconds, 0.5f));
  ARCANE_ASSERT_TRUE(near(clip.frames[1].duration_seconds, 0.1f));
  ARCANE_ASSERT_TRUE(near(clip.frames[0].uv.u0, 0.75f));
  ARCANE_ASSERT_TRUE(clip.name == "run" && clip.loop);
}

void test_save_load_round_trip() {
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_clip_authoring";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  ClipDraft draft;
  draft.name = "idle";
  draft.grid = {.columns = 2, .rows = 2};
  draft.loop = false;
  draft.add_range(0, 3);
  const AnimationClip clip = build_clip(draft, 32, 32);
  ARCANE_ASSERT_TRUE(save_clip_file(dir / "idle.json", clip).has_value());
  auto loaded = load_clip_file(dir / "idle.json");
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->name == "idle" && !loaded->loop && loaded->frames.size() == 4);
  ARCANE_ASSERT_TRUE(near(loaded->frames[3].uv.u0, 0.5f) && near(loaded->frames[3].uv.v0, 0.5f));
  ARCANE_ASSERT_TRUE(!load_clip_file(dir / "missing.json").has_value());
  ARCANE_ASSERT_TRUE(!save_clip_file(dir / "no_such_dir" / "x.json", clip).has_value());
  std::filesystem::remove_all(dir);
}

// Sets the mtime to a fixed offset from "now" so each edit gets a distinct stamp on any filesystem.
void touch(const std::filesystem::path& path, int seconds_ahead) {
  static const auto base = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(path, base + std::chrono::seconds(seconds_ahead));
}

void test_cache_hot_reload_on_mtime_change() {
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_clip_hot_reload";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto path = dir / "walk.json";

  AnimationClip one;
  one.frames.push_back({.uv = {0, 0, 0.5f, 1}, .duration_seconds = 0.1f});
  ARCANE_ASSERT_TRUE(save_clip_file(path, one).has_value());

  AnimationClipCache cache;
  cache.set_assets(list_animation_clip_assets(dir));
  const AssetId id = asset_id_for_relative_path("walk.json");
  ARCANE_ASSERT_TRUE(cache.get(id) && cache.get(id)->frames.size() == 1);
  ARCANE_ASSERT_TRUE(cache.refresh() == 0);  // untouched file stays cached

  AnimationClip two = one;
  two.frames.push_back({.uv = {0.5f, 0, 1, 1}, .duration_seconds = 0.2f});
  ARCANE_ASSERT_TRUE(save_clip_file(path, two).has_value());
  // Force a distinct mtime even on coarse-resolution filesystems.
  touch(path, 100);
  ARCANE_ASSERT_TRUE(cache.refresh() == 1);
  ARCANE_ASSERT_TRUE(cache.get(id) && cache.get(id)->frames.size() == 2);

  // A file that becomes unparsable is reloaded as absent; fixing it recovers.
  { std::ofstream(path) << "{ broken"; }
  touch(path, 200);
  ARCANE_ASSERT_TRUE(cache.refresh() == 1);
  ARCANE_ASSERT_TRUE(cache.get(id) == nullptr);
  ARCANE_ASSERT_TRUE(save_clip_file(path, one).has_value());
  touch(path, 300);
  ARCANE_ASSERT_TRUE(cache.refresh() == 1);
  ARCANE_ASSERT_TRUE(cache.get(id) && cache.get(id)->frames.size() == 1);

  // Deleting the file drops the entry.
  std::filesystem::remove(path);
  ARCANE_ASSERT_TRUE(cache.refresh() == 1);
  std::filesystem::remove_all(dir);
}

void test_reopen_clip() {
  ClipDraft draft;
  draft.name = "walk";
  draft.grid = {.columns = 4, .rows = 2};
  draft.loop = false;
  draft.cells = {5, 1, 1, 7};
  draft.durations = {0.25f, 0.0f, 0.4f, 0.0f};
  draft.fps = 8.0f;
  const AnimationClip clip = build_clip(draft, 128, 64);
  auto reopened = draft_from_clip(clip, draft.grid, 128, 64);
  ARCANE_ASSERT_TRUE(reopened.has_value());
  ARCANE_ASSERT_TRUE(reopened->cells == draft.cells && reopened->name == "walk" && !reopened->loop);
  // Editing nothing and rebuilding reproduces the clip exactly (frame order, UVs, durations).
  const AnimationClip again = build_clip(*reopened, 128, 64);
  ARCANE_ASSERT_TRUE(again.frames.size() == clip.frames.size());
  for (std::size_t i = 0; i < clip.frames.size(); ++i) {
    ARCANE_ASSERT_TRUE(near(again.frames[i].uv.u0, clip.frames[i].uv.u0) && near(again.frames[i].uv.v1, clip.frames[i].uv.v1));
    ARCANE_ASSERT_TRUE(near(again.frames[i].duration_seconds, clip.frames[i].duration_seconds));
  }
  // Arbitrary UVs or zero duration are rejected rather than silently snapped to a cell.
  AnimationClip odd = clip;
  odd.frames[2].uv = {0.1f, 0.1f, 0.2f, 0.2f};
  const auto bad_uv = draft_from_clip(odd, draft.grid, 128, 64);
  ARCANE_ASSERT_TRUE(!bad_uv.has_value() && bad_uv.error().find("frame 2") != std::string::npos);
  AnimationClip zero = clip;
  zero.frames[0].duration_seconds = 0.0f;
  ARCANE_ASSERT_TRUE(!draft_from_clip(zero, draft.grid, 128, 64).has_value());
  // A different grid does not match either.
  ARCANE_ASSERT_TRUE(!draft_from_clip(clip, SheetGrid{.columns = 3, .rows = 3}, 128, 64).has_value());
}

}  // namespace

int main() {
  test_reopen_clip();
  test_grid_slicing();
  test_draft_editing_and_build();
  test_save_load_round_trip();
  test_cache_hot_reload_on_mtime_change();
  return 0;
}
