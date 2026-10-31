## Franklin: how to test / validate

This PR (`engine-nightly-2026-10-30` -> `engine-hermes-2026-10-30`) is stacked on PR #8. Merge #7 and #8 first, then this one (retarget to master afterwards). It shows only tonight's diff. Jobs are appended per job below; HY1 (hygiene), AF10C (`arcane render`), AF8C (editor tileset/clip/settings through commands) GP1 (gameplay context API), GP2 (collision/trigger events, layers, queries, tile tags) and GP3 (camera follow, viewport size, parallax) have run so far.

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

GP3 extra check (Linux with Xvfb, or Windows with a GL desktop): `build-gcc/bin/arcane_player --project samples/platformer_slice --module build-gcc/bin/platformer_slice_game.so`, hold D. Expected: the view follows the hero with a small lead and smoothing, stops at the level edges (x 0..1280), and the hills/sky background scrolls slower than the ground (parallax, tiled). Headless: `ctest --test-dir build -C Debug -R "test_camera2d|test_game_context|test_platformer_slice|test_render_cli" --output-on-failure`. CLI: `arcane --project P component add --node id:N --type ParallaxLayer` then `prop set ... --name factor --value "[0.3, 1.0]"`.

GP1 extra check (any platform, no window needed): `ctest --test-dir build -C Debug -R "test_game_context|test_platformer_slice" --output-on-failure` expects both to pass. To see it live: run `build-gcc/bin/arcane_player --project samples/platformer_slice --module build-gcc/bin/platformer_slice_game.so`, collect a coin (it disappears and the score rises), walk into the pit with health left (you respawn at the start instead of dying at once; the second fall is game over), click Restart (the whole level reloads: coins are back, score 0).

GP2 extra check (any platform, no window): `ctest --test-dir build -C Debug -R "test_physics_events|test_tilemap_commands|test_tooling_commands|test_schema_examples" --output-on-failure` expects all four to pass. CLI: `arcane --project DIR project set --collision_layers '["world","player"]'` then `project get` shows them; `arcane --project DIR tileset set_tile --tileset assets/set.json --tile 1 --tags '["hazard"]'`. Not visible in a window: there is no editor UI for layers/tags beyond the Inspector's integer `layer`/`mask` fields.

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

### GP2: events, layers/masks, spatial queries, tile tags (slice gaps 3 and 9)
- `Component::on_collision_enter/exit` (renamed from `on_contact_*`) and new `on_trigger_enter/exit` (sensor Collider2D, Box2D sensor events); deterministic order (exits first, then by node id). `Collider2D.layer`/`mask`; names in `project.json` `collision_layers` (schema, `project.set`, undoable through EditSession). `PhysicsWorld::raycast/overlap_aabb/overlap_point` with layer mask, reachable via `GameContext::physics()` (set by the player and the fixed-frame runner). Tileset `tiles[].tags` (`tileset.set_tile --tags`), `GameContext::tile_at/tagged_tile_at`. Goldens: only `layer`/`mask` added to scene goldens; new `project.example.json`, `tileset.example.json`.
- Tests: `test_physics_events` (new), `test_tilemap_commands`, `test_tooling_commands`, `test_schema_examples`. `scripts/build-and-test.sh`: gcc PASS, clang ASan/UBSan PASS, mingw build PASS. UNTESTED on native Windows/MSVC.
- Not done: the platformer slice still uses its manual AABB test (not replaced); no editor widgets for layer names or tile tags; sensors do not detect static bodies' shapes beyond what Box2D allows.

### GP1: gameplay context API (docs/slice_platformer_gaps.md items 1, 2, 5, 6)
- `RigidBody2D::teleport(pos)` moves the node at once and queues a Box2D transform applied at the start of the next `PhysicsWorld::step` (velocity cleared), so it is safe inside any hook. `PhysicsWorld::step` now also drops bodies of destroyed nodes and creates bodies for nodes spawned since the last step (before this a destroyed node kept a ghost body).
- `Component::scene()` / `Node::scene()` give gameplay its `Scene` (`destroy_node` stays deferred to the end of the tick; `create_node`/`add_component` work from hooks; new `Scene::find_node_by_name`). `Scene` move ctor/assignment are now out-of-line and re-point every node.
- New `arcane/assets.hpp` (moved from `arcane_editor/asset_catalog`, editor keeps aliases): asset ids, listings and `find_asset_path`. New `arcane/game_context.hpp`: `GameContext` (project dir, `asset_path`, `play_sound` by id or by assets-relative path, `spawn_prefab`, `request_restart`), `active_game_context()`, `reload_scene_file`. `AudioSource::play()` and `play_one_shot(id)` use it; paths are absolute, so the working directory no longer matters. The player (window + `--headless`), `arcane run/test/render` and the editor's Play install a context.
- Restart: the runner reloads the authored scene file between ticks (`FixedRunOptions::restart_scene_json` for the headless runners, `reload_scene_file` in the player, stop+play in the editor so it restarts from the editor's in-memory scene). `SceneManager` is not used by the player/editor, so the restart is a small request flag instead of going through it.
- `platformer_slice`: coins use `destroy_node`, pit fall respawns with `teleport` (second fall = game over; test expects 2 hurt cues), Restart requests a real reload, sounds go through `play_sound("coin.wav")`.
- No authored data or registry commands were added, so no schema/command rows were needed. New test `tests/test_game_context.cpp` (teleport, destroy/spawn vs bodies, scene pointer through moves, asset lookup + audio by id, restart); `test_platformer_slice` updated. Tests: gcc PASS, clang ASan/UBSan PASS, mingw build PASS. UNTESTED on native Windows/MSVC; real audio device playback untested.

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


## GP3: camera follow, viewport size, parallax

- `Camera2D` (the existing follow camera) gained `look_ahead` (Vec2) and `bounds_tilemap` (node id; bounds = that Tilemap's world rectangle). No second `CameraFollow2D` component (see docs/decisions.md). Both are registered properties, so `prop.set`, the Inspector and JSON v2 already cover them; `docs/formats/scene.md` documents them.
- New `ParallaxLayer` component (factor Vec2, repeat_x/repeat_y) next to a Sprite, drawn in `render_scene` shifted by `camera * (1 - factor)` and tiled over the view, correct with zoom. Sprites only, not Tilemaps.
- `GameContext::viewport_size()` for modules (player, editor and `arcane render` set it; default 800x600).
- platformer_slice: the module no longer moves the camera; `MainCamera` is a Camera2D following the Player (smoothing, dead zone, look-ahead, bounds 0..1280 x 0..600) and a `Sky` ParallaxLayer (new `assets/sky.png`, factor 0.3, repeat_x). Goldens `tests/golden/render/platformer_slice_f30.png` and `tests/golden/platformer_slice/scenes/main.scene.json` regenerated; `test_render_cli` also checks that changing the parallax factor changes the render.
- UNTESTED on native Windows/MSVC (Linux gcc/clang/mingw only); the render golden assumes Mesa llvmpipe.
