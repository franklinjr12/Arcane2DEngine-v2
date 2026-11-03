## Franklin: how to test / validate

This PR (`engine-nightly-2026-11-01` -> `engine-nightly-2026-10-31`) is stacked on PR #11 (merge order #7 -> #8 -> #9 -> #11 -> this). Tonight so far: IN7 (ENG-037, input rebinding + deterministic replay), IN8 (ENG-038, tween easings, chaining, cancel), IN10 (ENG-040, save slot recovery), IN11 (ENG-041, log level filter + deterministic file sink), IN12 (ENG-042, property registry introspection + round trip), IN13 (ENG-043, canonical_json fuzz + idempotence tests), IN14 (ENG-044, platformer slopes), IN15 (ENG-045, fixed timestep: step clamp counters, alpha in [0,1), time_scale, invalid dt rejected; additive API in time.hpp).

**1. Step-by-step manual checks**

Linux: `scripts/build-and-test.sh` (expect gcc PASS, clang_san PASS, mingw build only). Quick: `ctest --test-dir build-gcc -R test_input --output-on-failure`.
Windows / MSVC (PowerShell, repo root):
```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64     # or "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug -R "test_input|test_tween" --output-on-failure   # expect pass
```

GP5 extra check (any platform, no window): `ctest --test-dir build -C Debug -R "test_topdown_controller|test_navigation|test_spawner|test_combat|test_topdown_slice|test_topdown_commands" --output-on-failure` expects all six to pass. To play (needs a display or Xvfb): `build-gcc/bin/arcane_player --project samples/topdown_slice --module build-gcc/bin/topdown_slice_game.so`, move with WASD and shoot with Space (the bullet flies where you last moved). The red enemy walks around the interior wall to reach you (each touch costs 1 health, shown in the HUD with a particle burst); three bullets kill it; at 0 health the scene restarts. CLI: `arcane --project samples/topdown_slice --module build-gcc/bin/topdown_slice_game.so prefab info --node path:/Arena/Enemy`.

GP6 extra check (any platform, no window): `ctest --test-dir build -C Debug -R "test_platformer_slice|test_schema_examples|test_cli|test_migration" --output-on-failure` expects all to pass; with Xvfb or a GL desktop also `-R "test_platformer_replay|test_render"` (the replay trace and the render golden are unchanged). Live: `build-gcc/bin/arcane_player --project samples/platformer_slice --module build-gcc/bin/platformer_slice_game.so`, press P/Escape (or gamepad Start): the game freezes and RESUME / RESTART / MENU buttons appear (D-pad down + A, or the mouse); MENU opens the menu scene, PLAY starts the level. Resize the window: the 640x360 view letterboxes with black bars and the buttons are still clicked where they are drawn.

**2. What you can visibly see**

PX1: Hierarchy "Save as Prefab", Assets > Prefabs list that instantiates on click, `[P]` marker on instance nodes, Inspector prefab header with per-override Revert, "* overridden" tags and Break prefab link.

GS2: the platformer sample now has sensor-trigger pickups/spikes/goal, checkpoints with pit respawn, a patrolling enemy, hero jump/fall/hurt animation states, pause (P/Escape), camera follow + parallax and a second level reached through the goal (score carries over), plus a replay test (`test_platformer_replay`, GL-labeled) and gaps in `docs/slice_platformer_gaps.md`.

GS1: the top-down sample is now an arena with waves, coin pickups, HUD, pause, game over/restart, a 60 s replay test (`test_topdown_replay_60s`) and perf numbers in `docs/perf.md`.

GP5: the top-down sample plays with WASD + Space: bullets from a pooled spawner, an enemy that paths around an interior wall, player health in the HUD, restart on death.

GP6: the platformer pauses through a game time scale (physics, animation and enemies stop; the player stays put), with a centred Resume/Restart/Menu column navigable with the D-pad and A; the gamepad can also move (D-pad or left stick), jump (A) and pause (Start). `scenes/menu.scene.json` shows the title, `BEST <n>` and PLAY; winning or game over writes the best score (saves are on only in the windowed player and editor Play) and shows it as `BEST <n>` under the score. The window is resizable with letterbox bars at 640x360.

GP4: new Animation Graph editor panel; `hero.animgraph.json` driving the hero's idle/run clips; the hero mirrors when moving left (sprite flip, collider follows); Sprite has `flip_x`/`flip_y` and Collider2D `mirror_with_sprite` in the Inspector.

AF8C: the new Project Settings sections and undoable Tileset/Clip Save described above.

AF10C: the PNG above. HY1: no feature change. The 1.4 MB build artefact `samples/topdown_slice/topdown_slice_game.so` is no longer tracked, and `.gitignore` covers it (and the `.dll`).
IN6: `docs/perf_gate.md`, `docs/perf_baseline.json`, `ctest -L perf`.
No UI change. New: `engine/include/arcane/atomic_save.hpp` and `test_atomic_save` (a simulated kill after every step of a save). Saves now briefly create a `.arcane-commit` file in the project root.
No UI change. IN8: `Tween` components accept 7 new easing names (`back_in`, `back_in_out`, `elastic_in|out|in_out`, `bounce_in`, `bounce_in_out`); new `TweenSystem::start_sequence/start_parallel/start_node_sequence/start_node_parallel` and `cancel(id, snap_to_end)`. IN7: new runtime API in `arcane/input.hpp`: `InputActionMap::rebind`, `clear_bindings(name, device)`, `capture_defaults`/`reset_to_defaults`, and `InputRecording`; new cases in `tests/test_input.cpp`.

IN10: no UI change; `arcane/save_game.hpp` gains `load_save_game_recovering` (falls back to `<slot>.save.json.bak`, reports why), `delete_save_game`, `save_backup_path`; `write_save_game` keeps the previous good file as `.bak` and refuses to overwrite a file from a newer engine. Tests: `ctest -R test_save_game`.
IN11: no UI change; `arcane/log.hpp` gains `log_set_min_level`, `log_set_category_level`, `log_would_log`, `log_add_sink`/`log_remove_sink`, `log_set_stderr_enabled`, `log_flush`, and `log_set_file(path, LogFileOptions{max_bytes,max_files,timestamps})`. File lines are `LEVEL|category|message` (no wall-clock unless enabled). Tests: `ctest -R test_logger`.
IN12: no UI change; `arcane/property.hpp` gains `describe_components(registry)` (types sorted by name, properties in registration order with type and default), `get_property_by_name`/`set_property_by_name` (unknown type/property and wrong value type are errors, data untouched) and `value_fits_type`. `tests/test_property_roundtrip.cpp` sets non-default values on every registered type (built-ins + sample_game), saves, reloads and compares values and bytes. Run `ctest -R test_property_roundtrip`. component_registry.* untouched.
IN13: no UI change; `ctest -R test_canonical_json` now runs 12,000 seeded fuzz iterations (~0.6 s: random nesting, unicode escapes, int64/uint64 extremes, binary32 edge floats, shuffled keys; checks idempotence, value round trip, sorted keys, truncation and random byte damage). One real bug found and fixed in `engine/src/canonical_json.cpp`: deeply nested input (e.g. 200000 `[`) overflowed the stack and crashed; nesting deeper than 512 levels is now an error. Documented in `docs/formats/canonical-json.md`.
IN16 (ENG-046): no UI change; `arcane/animation.hpp` gains runtime-only `AnimationClip::ping_pong` (clip format unchanged), `clip_finished`, `frame_events(clip, from, to)` (frames entered, once per crossing, forward/reverse, multi-frame skips, capped at 64 periods) and `AnimatedSprite::previous_elapsed_seconds`; speed 0 freezes, negative reverses, non-finite advance ignored. Run `ctest -R test_animation`.
IN17 (ENG-047): tests only, no UI change; `test_game_context` gains deferred destroy / double destroy / find-after-destroy, 1000-node deterministic spawn/destroy churn (run under ASan) and restart-drops-pending cases. Run `ctest -R test_game_context`.
IN18 (ENG-048): save slots gain optional `meta` (display name, play time, injected timestamp), `read_save_slot_info` (header+meta only, falls back to .bak) and `list_save_slot_infos` (sorted by timestamp, then name). No UI. Run `ctest -R test_save_game`. Note: `schemas/arcane.save.schema.json` (hermes-owned) and docs/formats/save.md do not yet list `meta`.

IN14: no UI change; `PlatformerController2D` walks up/down slopes (up to `max_slope_deg`, default 50) without sliding or launching, using a downward raycast through the active GameContext's PhysicsWorld. Coyote time / jump buffer / jump cut already existed; new cases in `tests/test_platformer.cpp` (`ctest -R test_platformer`). `max_slope_deg` is a C++ member only, not a serialized property (would alter goldens).

**3. Enough to merge**
Minimum: Windows build + `test_input` / full ctest green. Optional: nothing manual.

**4. Known risks**

- PX1: the new prefab widgets (ImGui) are compile-checked only; the command/EditSession paths are tested headless. Propagation happens on load only and ignores added/removed nodes or components; prefab nesting is not supported. UNTESTED on native Windows/MSVC.

- GP5: UNTESTED on native Windows/MSVC. Nobody played the top-down slice by hand: the GL tests (`test_export_topdown_slice`, `test_player_topdown_frames`) and the fixed-frame run only prove it loads and steps, and `test_topdown_slice` drives it with fake input. Not covered: enemy spawner waves, line of sight, knockback, melee swings, steering around other enemies (no separation in the slice).

- GP6: UNTESTED on native Windows/MSVC. The gamepad bindings and D-pad/A navigation are tested with the fake input backend only, not with real hardware (Start is GLFW button 7, which `arcane::GamepadButtons` has no constant for). The menu scene is not the startup scene (see `docs/slice_platformer_gaps.md`). Resizing and the letterbox bars were not looked at on a real window. The level HUD's own RESTART button is disabled while the pause menu is open.

- GP4: the Animation Graph panel (ImGui) is compile-checked only and its commands are tested headless; nobody clicked it. Flip visuals are not golden-tested (no sprite sheet with asymmetric art in the render goldens). Adding `Sprite`/`Collider2D` properties changed the canonical text of scenes that contain them (samples and `tests/golden/` were regenerated; old files still load). Rebuilding a mirrored collider on flip can emit one collision exit/enter. UNTESTED on native Windows/MSVC.

- AF8C: the editor widgets (ImGui) were compile-checked and the command/EditSession paths are tested headless, but the panels themselves were not clicked through by a human. `transition.set`, `save.config_set` and `debug.set` changed from immediate `project.json` writes to in-memory edits (CLI output the same; `--dry-run` now allowed). Tileset/clip draft fields before Save are still unsaved panel state (documented, by design). UNTESTED on native Windows/MSVC.

- AF10C `arcane render` is UNTESTED on native Windows/MSVC (GL offscreen FBO path ran only on Linux Mesa llvmpipe under Xvfb); the golden `tests/golden/render/platformer_slice_f30.png` is llvmpipe-specific and other GPUs may need a regenerated golden or higher tolerance.

- UNTESTED on native Windows/MSVC for this PR (only the mingw cross-build ran).
- The `.so` stays in the history of PR #7/#8 (commits 49962be, 2e78c85, eb931a1); history was not rewritten, so the repo size is unchanged. The file is only removed from the index.
- Anyone who relied on the committed `.so` must build once so CMake copies it in.
- UNTESTED on native Windows/MSVC (only mingw cross-build ran). On Windows `_commit` is used for fsync and directory fsync is skipped.
- Opening a project removes stale `*.arcane-tmp` files under it; a second process saving into the same project at that moment would see its save fail (rolled back, no corruption). Concurrent writers are not supported.
- Single-file saves (`write_text_file_atomic`) are unchanged (rename of a synced-less temp); only the multi-file paths (editor save, CLI save, migration) use the journal. `canonical_json.hpp` still carries the old "not crash-atomic" note on `write_text_files_transactional`, which is now unused by product code (hermes IN2 owns that file tonight).
- UNTESTED on native Windows/MSVC (only the mingw cross-build ran).
- IN8: easing endpoints are now exact (`ease(e,0)==0`, `ease(e,1)==1`); `Easing` enumerator values were renumbered (names, which are what files store, are unchanged). `cancel`/`cancel_node` gained a defaulted `snap_to_end` parameter.
- Additive API only; input map file format and loader unchanged.
- IN17 (ENG-047) edited `tests/test_game_context.cpp`, which is on the hermes do-not-edit list; it is tests-only and the owner should review it.
- Known engine gap (pinned by IN17, not fixed): replacing a Scene on restart does not call `on_destroy` for runtime nodes, although the `game_context.hpp` comment says it does. The fix needs `scene.cpp`, which was out of scope.
- ENG-039 (text layout) was dropped from this PR: the board row is owned by another agent; the commit is preserved on branch side/eng-039-in9-2026-11-01.

---

## Technical detail

### IN13: canonical_json fuzz
Fixed-seed mt19937_64 generator in `tests/test_canonical_json.cpp`; only `canonicalize_json` is public, so values are emitted as text with random whitespace/escapes/key order and checked against an nlohmann model. A leading UTF-8 BOM is accepted and dropped (nlohmann behaviour, left as is). Write/transaction path untouched.

### GP5: top-down controller, steering, combat, pooled spawner (docs/slice_topdown_gaps.md)
- Engine components (all registered by `register_physics_components`): `TopDownController2D` (`topdown.hpp`), `SteeringAgent2D` + `NavGrid`/`find_path` grid A* (`navigation.hpp`), `Spawner2D` + `Node::set_active` (`spawner.hpp`), `Health`/`Hurtbox`/`Hitbox`/`Projectile` (`combat.hpp`). Decisions 82-85 and 86 in `docs/decisions.md`.
- `samples/topdown_slice`: `project.json` gets `collision_layers` world/player/enemy/player_attack/enemy_attack and a `fire` action on Space. Player = TopDownController2D + Health(5) + Hurtbox (layer player). Enemy = `assets/enemy.prefab.json` (SteeringAgent2D to `Player` with pathfinding, Health(3), Hurtbox, Hitbox for contact damage); the scene instances it as `Enemy`. A short wall (column 9, rows 4-10) sits between player and enemy so A* matters. `BulletSpawner` pools `assets/bullet.prefab.json` (sensor collider, Hitbox 1, Projectile).
- `ArenaRules` is now glue only: Space -> `Spawner2D::spawn` + `Projectile::launch(facing)`, HUD from the player's `Health`, a `Health` listener for the HitBurst and `GameContext::request_restart` on death. Properties: `player_speed`/`enemy_speed` are copied into the controller/agents at start, `bullet_speed` is new; `hit_radius`, `hit_cooldown` and `hit_damage` are removed (damage is the enemy Hitbox's). The scene was edited with `arcane batch`/`prefab save`/`prefab instantiate`.
- The enemy's body collider is on layer `enemy` (mask world+player+player_attack) so bullets hit it; its contact Hitbox shares that collider, so `enemy_attack` is declared but no collider uses it yet. One collider per node means a separate attack collider would need a child body.
- Tests: `test_topdown_slice` (left wall, A* detour, enemy hurts player, Space bullet damages and kills the enemy, HUD and particles), `test_topdown_commands` (3 properties), plus the component tests. `scripts/build-and-test.sh` result in the commit message / final report.

### GP4: animation state machine, sprite flip, HUD lookup (slice gaps 4, 7, 10)
- `arcane.animgraph` v2 (`docs/formats/animgraph.md`, `schemas/arcane.animgraph.schema.json`, example in `tests/golden/schema_examples/`): parameters (bool/float/trigger), states -> clip ids with speed and frame events, transitions with priority and conditions. `Animator` component drives the sibling `AnimatedSprite`; gameplay calls `set_bool/set_float/set_trigger`; events reach `Component::on_animation_event`. Migration re-checks graphs; `validate` checks them strictly.
- Commands `animgraph.create/info/add_param/remove_param/add_state/set_state/remove_state/add_event/remove_event/add_transition/remove_transition/restore` (docs/commands.md, regenerated cli-reference.md). Editor: Animation Graph panel through `EditSession`.
- `Sprite::flip_x/flip_y` (render swaps UVs), `Collider2D::mirror_with_sprite` (offset mirrored, shape rebuilt on change).
- `Hud::find_text/find_bar/find_image/find_button`; HUD ids already existed. platformer_slice uses all three (module no longer has clip properties or negative scale).
- Tests: `test_animgraph`, `test_animgraph_commands`, extended `test_platformer_slice`, `test_physics_components`, `test_schema_examples`, `test_migration`.
### Delegated decisions tonight
- Branch stacked on `engine-nightly-2026-10-30` (PR #9), not on hermes branches (docs/decisions.md).
- IN6 baseline is per flavor (`Debug-GNU`); other flavors skip the perf gate rather than compare against foreign timings.
- IN4 manifest name `.arcane-commit` and temp suffix `.arcane-tmp` chosen by us; recovery runs at every open.
### IN10: save slot recovery
- `write_save_game`: old file valid -> copied atomically to `.bak` first; corrupt old file never replaces a good `.bak`; newer file `version` is refused untouched. Existing `load_save_game`/`read_save_game_file` and all error texts unchanged.
- No checksum field: the `arcane.save` v2 schema is strict (schemas/* is hermes-owned tonight), so truncation/garbage is detected by the strict parse instead. Future data_version refused by `migrate_save_game` without fallback.
- Tests: `tests/test_save_game.cpp` (list/delete, upgrade, truncated->.bak, garbage, future version, no temp leftovers). gcc PASS, clang ASan/UBSan PASS, mingw build PASS. UNTESTED on native Windows/MSVC.

### IN8: tween easings, chaining, cancel
- Easing set complete: in/out/in-out for quad, cubic, sine, back, elastic, bounce (+linear). Endpoints exact for all.
- Chains: a sequence/parallel group is one `TweenId`; step N+1 of a sequence starts on a computed timeline offset (no drift); per-step `spec.delay` supported; `on_complete` once after the last step. Sequence with a non-last endless step is rejected (returns 0).
- Cancel: stops the rest of a chain, never calls `on_complete`; optional `snap_to_end` applies every step's final value (endless steps skipped); `cancel_node` covers chains bound to the node.
- Tests: `tests/test_tween.cpp` per-easing table (endpoints, clamping, monotonic where expected, golden samples at 0.25/0.5/0.75 computed independently in Python), sequence/parallel timing at fixed dt, cancel/snap/cancel-in-callback/cancel_node tests.
- gcc Debug PASS, clang ASan/UBSan PASS, mingw build PASS. UNTESTED on native Windows/MSVC.

### GP1: gameplay context API (docs/slice_platformer_gaps.md items 1, 2, 5, 6)
- `RigidBody2D::teleport(pos)` moves the node at once and queues a Box2D transform applied at the start of the next `PhysicsWorld::step` (velocity cleared), so it is safe inside any hook. `PhysicsWorld::step` now also drops bodies of destroyed nodes and creates bodies for nodes spawned since the last step (before this a destroyed node kept a ghost body).
- `Component::scene()` / `Node::scene()` give gameplay its `Scene` (`destroy_node` stays deferred to the end of the tick; `create_node`/`add_component` work from hooks; new `Scene::find_node_by_name`). `Scene` move ctor/assignment are now out-of-line and re-point every node.
- New `arcane/assets.hpp` (moved from `arcane_editor/asset_catalog`, editor keeps aliases): asset ids, listings and `find_asset_path`. New `arcane/game_context.hpp`: `GameContext` (project dir, `asset_path`, `play_sound` by id or by assets-relative path, `spawn_prefab`, `request_restart`), `active_game_context()`, `reload_scene_file`. `AudioSource::play()` and `play_one_shot(id)` use it; paths are absolute, so the working directory no longer matters. The player (window + `--headless`), `arcane run/test/render` and the editor's Play install a context.
- Restart: the runner reloads the authored scene file between ticks (`FixedRunOptions::restart_scene_json` for the headless runners, `reload_scene_file` in the player, stop+play in the editor so it restarts from the editor's in-memory scene). The windowed player and the editor's Play mode own a `SceneManager` (PX2: `active_scene_manager()->request_switch(...)`, authored persistent roots carry across scenes); the headless runners (`--headless`, `arcane run`) do not yet. Restart stays a small request flag rather than a manager switch.
- `platformer_slice`: coins use `destroy_node`, pit fall respawns with `teleport` (second fall = game over; test expects 2 hurt cues), Restart requests a real reload, sounds go through `play_sound("coin.wav")`.
- No authored data or registry commands were added, so no schema/command rows were needed. New test `tests/test_game_context.cpp` (teleport, destroy/spawn vs bodies, scene pointer through moves, asset lookup + audio by id, restart); `test_platformer_slice` updated. Tests: gcc PASS, clang ASan/UBSan PASS, mingw build PASS. UNTESTED on native Windows/MSVC; real audio device playback untested.
### IN6: headless bench regression gate (ENG-036)
- `tests/perf_gate.cmake` (cmake -P) runs `arcane_bench` for scene/particles/physics at default sizes, 5 runs each, median `avg_frame_ms`, fails above baseline +30% (`docs/perf_baseline.json`). Tests `perf_gate` and `perf_gate_forced_slow` (WILL_FAIL, 1000x scale) carry label `perf`; baseline refresh target `perf_baseline_update`. Baseline is for flavor `Debug-GNU`; other flavors skip. Docs in new `docs/perf_gate.md` (hermes owns docs/perf.md and bench_main.cpp tonight, untouched).
- UNTESTED on native Windows/MSVC (the gate skips there anyway: different flavor).

### IN5: CLI exit-code and error contract test (ENG-035)
- New `tests/test_cli_contract.cpp`: for every registry and offline command (generated list) runs `arcane <cmd> --args <invalid>` on a copy of `samples/sample_project`; asserts exit 2, `ok:false` with `code/message/file/path/hint`, `ARGS_INVALID`, single-line stdout, stderr diagnostic, and no project file changed. Also pins `exit_code()` for every code and checks each is listed in its row of `docs/cli-reference.md`. The exit table already existed in the generator, so no generator change.
- Tests: gcc Debug and clang ASan/UBSan green, mingw build OK. UNTESTED on native Windows/MSVC.

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

## GP6: pause menu, menu scene, best score, design resolution (platformer slice)

- `SliceRules::set_paused` now calls `GameContext::set_time_scale(0/1)` (no more node deactivation); the runners keep updating components at dt 0, so `pause` and the HUD buttons are still read. Without a `GameContext` pausing is a no-op.
- Pause/end-of-run menu: HUD buttons `pause_resume`, `pause_restart`, `pause_menu` (prefixed because the project HUD already owns `restart`) added with `Hud::add` and removed with `Hud::remove` on resume, restart, scene switch (`on_destroy`) and reload. The corner `restart` button is disabled while the menu is open so D-pad down starts at the first menu button (`HudInput` only focuses on a nav press, never on its own).
- `MenuController` (module component, `scenes/menu.scene.json`): sets the level HUD elements aside, adds `menu_title`, `menu_best`, `menu_play`, and restores the level HUD in `on_destroy`; PLAY switches to `scenes/main.scene.json`. Reached from the pause menu's MENU and from the win/game-over menu.
- Best score: `project.json` `save` (`arcane_platformer_slice`), slot `progress`, key `best_score`; written on the final level's goal or game over only if higher; save errors are logged (warning only when saves are enabled).
- `project.json` also gets `display` 640x360 letterbox and gamepad bindings (D-pad/left stick move, A jump, Start pause). Replay trace and render golden stay valid (`arcane render` passes `--size` in the test); `tests/golden/platformer_slice/project.json` was regenerated for the new keys. `make_assets.py` generates `project.json` and the menu scene.
- Tests: `test_platformer_slice` (time scale 0 pause, buttons appear/disappear, D-pad + A and mouse Resume, Restart/Menu/PLAY flow with the HUD swap, best score written/kept/raised under `ARCANE_SAVE_DIR`, saves off writes nothing). Top-down pause unchanged.

## PF1: rendering and entity performance

- Sprite batching (static index buffer, VBO orphaning, 16384-quad batches), view culling with `RenderStats::culled`, benchmark defaults 5000 sprites / 2000 particles / 500 bodies, `arcane_bench --render 1`. Tests: `test_render_bench`, `test_render_cull`, `test_debug`, `bench_cli_render_smoke`. Numbers in `docs/perf.md` come from llvmpipe under Xvfb; Windows not measured.
### IN4: crash-atomic multi-file save (ENG-034)
- New `arcane/atomic_save.hpp` + `engine/src/atomic_save.cpp`: `commit_files_atomic(root, files, options)` stages every file as `NAME.arcane-tmp` (fwrite + fsync), writes a manifest `ROOT/.arcane-commit` (itself via temp + rename = the commit point), renames the staged files, fsyncs the root, removes the manifest. `recover_pending_commit(root)` rolls a committed-but-unfinished save forward, or discards a missing/torn manifest and all stale `.arcane-tmp`. A kill therefore yields entirely old or entirely new files.
- Ordinary failure while renaming still restores previous bytes and reports `rolled back: ...` (existing tests pass unchanged).
- Call sites: `ProjectSession::save` (editor), `ProjectContext::save` (CLI/tooling), `migrate_project`; recovery runs in `ProjectSession::open` (so editor, player, CLI) and at the start of every commit.
- Test `tests/test_atomic_save.cpp`: crash hook (`crash_at_step`) after each of the 2N+2 steps, recover, assert all-old before the commit point / all-new after, no leftovers; torn manifest; next save recovers; rollback; path outside root.
- Tests: gcc Debug and clang ASan/UBSan green (ctest all passed), mingw cross-build OK.
### IN7: input rebinding + deterministic replay
- `rebind(action, binding, allow_conflict=false)`: replaces that device's bindings, reports conflicting actions instead of overwriting.
- `capture_defaults()` / `reset_to_defaults([action])`: in-memory snapshot, not saved to file.
- `InputRecording`: per-tick action states as bytes; 600-tick scripted run recorded twice and replayed gives identical bytes.

## Test results (WRAP, final commit)
- Final commit on this branch, gcc Debug (`build-gcc`, incremental build + headless CTest): 78/78 tests pass; `arcane --project samples/{platformer_slice,sample_project,topdown_slice} run --frames 60` returns `ok:true`.
- Per-job runs (jobs/IN15..IN18/RESULT.md): gcc, clang ASan/UBSan and mingw cross-build reported PASS for each job. Clang and mingw were not re-run in WRAP.
- Native Windows/MSVC: UNTESTED.
