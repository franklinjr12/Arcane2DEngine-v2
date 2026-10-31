## Franklin: how to test / validate

This PR (`engine-nightly-2026-10-30` -> `engine-hermes-2026-10-30`) is stacked on PR #8. Merge #7 and #8 first, then this one (retarget to master afterwards). It shows only tonight's diff. Jobs are appended per job below; HY1 (hygiene), AF10C (`arcane render`) and AF8C (editor tileset/clip/settings through commands) have run so far.

**1. Step-by-step manual checks**

Linux:
```
scripts/build-and-test.sh     # expect: gcc PASS, clang_san PASS, mingw PASS (build only)
git ls-files samples | grep -E '\.(so|dll)$'    # expect: no output
```
Windows / MSVC (PowerShell, repo root):
```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64     # or "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure       # expect: all tests pass
```
After a build, `samples/topdown_slice/topdown_slice_game.so` (or `.dll`) appears in the work tree but `git status` stays clean.

AF10C extra check (Linux, needs a display or Xvfb; on Windows run it from a Developer PowerShell with a GL-capable desktop):
```
xvfb-run -a build-gcc/bin/arcane --project samples/platformer_slice --module build-gcc/bin/platformer_slice_game.so render --output out/slice.png --size 320x180 --frames 30
```
Expected: JSON with `"ok":true`, `scene_hash`, `backend`; open `samples/platformer_slice/out/slice.png`: yellow coins, brown platform, red bar and score text on a dark background (delete the `out/` folder afterwards). With `env -u DISPLAY -u WAYLAND_DISPLAY` the same command prints `RENDER_UNAVAILABLE` and exits 8.

AF8C extra check (Linux, or Windows with a GL desktop): `build-gcc/bin/arcane_editor --project samples/platformer_slice`, then
- Project Settings panel: below Name/Main scene/Module there are now Scene transition (fade out/in, colour), Save (game id, data version) and Debug overlay (visible, font scale). Change one: the title shows modified, Edit > Undo (Ctrl+Z) reverts it, File > Save writes `project.json`.
- Tileset panel: New tileset / edit fields / Save tileset, then Ctrl+Z: the file disappears again (or returns to its previous content). Animation Clip panel: Save clip then Ctrl+Z likewise; Open clip reloads a saved clip on the current grid.

**2. What you can visibly see**

AF8C: the new Project Settings sections and undoable Tileset/Clip Save described above.

AF10C: the PNG above. HY1: no feature change. The 1.4 MB build artefact `samples/topdown_slice/topdown_slice_game.so` is no longer tracked, and `.gitignore` covers it (and the `.dll`).

**3. Enough to merge**

Minimum: `scripts/build-and-test.sh` (or the Windows ctest above) is green. Optional: open `arcane_editor --project samples/topdown_slice` and confirm it still loads (the module is rebuilt by CMake).

**4. Known risks**

- AF8C: the editor widgets (ImGui) were compile-checked and the command/EditSession paths are tested headless, but the panels themselves were not clicked through by a human. `transition.set`, `save.config_set` and `debug.set` changed from immediate `project.json` writes to in-memory edits (CLI output the same; `--dry-run` now allowed). Tileset/clip draft fields before Save are still unsaved panel state (documented, by design). UNTESTED on native Windows/MSVC.

- AF10C `arcane render` is UNTESTED on native Windows/MSVC (GL offscreen FBO path ran only on Linux Mesa llvmpipe under Xvfb); the golden `tests/golden/render/platformer_slice_f30.png` is llvmpipe-specific and other GPUs may need a regenerated golden or higher tolerance.

- UNTESTED on native Windows/MSVC for this PR (only the mingw cross-build ran).
- The `.so` stays in the history of PR #7/#8 (commits 49962be, 2e78c85, eb931a1); history was not rewritten, so the repo size is unchanged. The file is only removed from the index.
- Anyone who relied on the committed `.so` must build once so CMake copies it in.

---

## Technical detail

### AF8C: tileset/clip panel Save, Open clip, settings widgets
- `transition.set`/`save.config_set`/`debug.set`: in-memory project edits (`mutable_project` + `mark_project_dirty`), `mutating`, no longer immediate writes (`tooling/src/flow_commands.cpp`, `all_commands.cpp`). New `tileset.replace` (whole tileset in one step) and `tileset.restore` (inverse; `tileset.create` returns it too) in `tilemap_commands.cpp`.
- `editor/app/editor_main.cpp`: Save tileset = one `tileset.replace`, Save clip = `clip.create` via `EditSession`, Open clip = `clip.info`, Open tileset = `tileset.info`, Project Settings gets transition/save/debug widgets.
- Tests: `test_edit_session` +3 (flow settings parity/undo/save, tileset Save undo, clip open/save undo) and a wider source lint; `test_scene_flow`/`test_debug` now persist explicitly. Docs: `docs/commands.md`, regenerated `docs/cli-reference.md`, inventory, decisions.

### AF10C: `arcane render`
- New `tooling/src/render_commands.cpp` (command `render`), `render_offscreen` and masked `compare_images` in `engine/include/arcane/screenshot.hpp`, `run_on_copy` shared with `run`/`test` (optional `finish` callback), exit code 8 / `RENDER_UNAVAILABLE`.
- Tests: `test_render_cli` (gl label: metadata, determinism, golden within tolerance 2, mismatch exit 4, size mismatch exit 4, mask) and `test_render_cli_nodisplay` (exit 8). Golden `tests/golden/render/platformer_slice_f30.png`.
- Docs: `docs/commands.md` (Render), regenerated `docs/cli-reference.md`, `AGENTS.md`, acceptance-audit row 3, decisions row.

### HY1: branch sanity, build hygiene, PR body
- Tip ac03c08 builds and passes `scripts/build-and-test.sh`: gcc Debug PASS, clang ASan/UBSan PASS, mingw cross-build PASS (build only, not run).
- The old FE18 wip problem (Color `operator==` in prefab.cpp) is fixed at the tip: `Color` has a defaulted `operator==` in `engine/include/arcane/renderer.hpp`.
- `git rm --cached samples/topdown_slice/topdown_slice_game.so`; `.gitignore` gets `/samples/topdown_slice/topdown_slice_game.so` and `.dll`. Other sample modules were already ignored.
- `docs/decisions.md`: row for the branch choice.
