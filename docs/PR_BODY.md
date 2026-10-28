# Foundation D — physics, input, animation, audio, HUD

## Summary

Stacked on `engine-nightly-2026-10-25` (Foundation C: editor + `--project`
player, draft PR #2). Adds Box2D-backed 2D physics and a serializable
input action map, both wired into Play mode (editor) and the standalone
player.

- **Physics** (`engine/include/arcane/physics.hpp`, `engine/src/physics.cpp`):
  `RigidBody2D` (body type, linear velocity, gravity scale, fixed rotation)
  and `Collider2D` (box/circle, offset, density/friction/restitution,
  sensor flag) components, both with registered properties so they appear
  in the Inspector and round-trip through scene JSON like any other
  component. `PhysicsWorld` owns one Box2D world per *runtime* scene:
  `rebuild()` creates bodies/shapes from every `RigidBody2D`(+`Collider2D`)
  node, `step()` advances the simulation and syncs `Transform2D`/
  `linear_velocity` back, and dispatches `Component::on_contact_begin`/
  `on_contact_end` for shapes that started/stopped touching. All pixel
  values stay in pixels everywhere outside `PhysicsWorld`; the px<->m
  conversion (32 px = 1 m, `docs/decisions.md` row 3) and the y-flip
  between screen space and Box2D's y-up world are both confined to that
  one boundary. Physics only ever runs against a runtime scene — the
  editor's authored scene in Edit mode is never handed to a `PhysicsWorld`,
  so Play/Stop's existing snapshot isolation (Foundation C) also isolates
  physics.
- **Input** (`engine/include/arcane/input.hpp`, `engine/src/input.cpp`):
  `InputActionMap` — named actions bound to zero or more physical inputs
  (keyboard/mouse button/gamepad button), `held()`/`pressed()`/`released()`
  edge-detected queries, `add_binding`/`clear_bindings`/`set_action` for
  rebinding, JSON (de)serialization embedded in `ProjectFile`/project.json.
  `GlfwInputBackend` polls a real `Window`; `FakeInputBackend` lets tests
  drive state directly with no window/platform input, so the whole action
  map is unit-testable headlessly. A single global `active_input_map()` is
  set by the editor/player main loops only while Play is actually running
  (null in Edit mode and after Stop), so gameplay code can query actions
  from `Component::on_update` without threading a reference through every
  call site, while the editor's own ImGui input in Edit mode is unaffected.
- **Sample**: `sample_game`'s `PlayerController` component reads
  `move_left`/`move_right`/`jump` from the active input map and drives a
  sibling `RigidBody2D`; `samples/sample_project` wires those three actions
  to WASD/arrow keys/space and gives the `Player` node a
  `RigidBody2D`+`Collider2D`+`PlayerController`.
- **Animation** (`arcane/animation.hpp`): `AnimationClip` JSON assets and an
  `AnimatedSprite` component; `render_scene` resolves the clip through
  `AnimationClipCache` and draws the selected frame's UV for nodes with
  `Sprite`+`AnimatedSprite`. The editor's Assets panel lists clips/sounds
  (click to assign to the selected node) and the Toolbar's "Preview
  animations" checkbox plays them in Edit mode.
- **Audio** (`arcane/audio.hpp`): miniaudio-backed `AudioSystem` + `AudioSource`
  component. Editor and player use the **null backend** (decisions row 27);
  `play_on_start` sources fire when Play begins / the player starts, and
  gameplay can fire one-shots through `active_audio_system()`. Tests never
  open a device.
- **HUD** (`arcane/hud.hpp`): `Hud` list of text (stb_easy_font, ASCII),
  image and progress-bar elements, anchor + offset layout scaled by screen
  height, drawn after the world by `HudRenderer` (via new
  `SpriteRenderer::flush()`); gameplay reaches it through `active_hud()`.
  Not yet: buttons, focus/input routing, Unicode text (doc 7.7 later work).
- Fixed-update wiring (`Component::on_update(float fixed_dt)` driven once
  per fixed step by `FixedTimestep`, in both `editor_main.cpp` and
  `player.cpp`) already existed before this branch (Foundation B/C) and
  needed no changes — physics and input both hook into it as-is.

## How to build/test

```sh
xvfb-run -a scripts/build-and-test.sh /tmp/a2d-check

# run the sample project standalone; WASD/arrows move, space jumps
xvfb-run -a build-gcc/bin/arcane_player --project samples/sample_project
```

## Test results (this PR, run under `xvfb-run -a`)

| Config | Result |
| ------ | ------ |
| gcc 14 Debug | **PASS** — 18/18 tests (4 `gl`-labeled, run for real via Xvfb/Mesa llvmpipe) |
| clang 19 + `ARCANE_SANITIZE=ON` (ASan+UBSan) | **PASS** — 18/18 tests |
| mingw-w64 cross-compile | **PASS** (build only) — not run on real Windows |

New tests this PR: `test_physics_headless` (bare Box2D v3 C API sanity,
no engine code), `test_physics_components` (falling body lands on a
static box and both sides see exactly one `on_contact_begin`; serialization
round-trip of `RigidBody2D`/`Collider2D` properties; `rebuild()` alone
never mutates the scene), `test_editor_state`'s
`test_play_stop_isolation_with_physics` (Play actually steps physics,
Stop restores the authored position untouched), `test_input` (edge
detection, OR'd bindings across devices, `clear_bindings`, JSON round-trip,
`active_input_map()` defaults to null), `test_scene_json`'s
`test_project_round_trip` extended to cover `input_actions`.

## Delegated decisions

Full table in `docs/decisions.md` (Foundation D section). Highlights:

- Input action map config is a nested object inside `project.json`, not a
  separate file — one project has exactly one map today.
- Gameplay reaches the live action map via a global `active_input_map()`
  pointer (non-null only during Play), not a parameter threaded through
  every lifecycle call.
- Gamepad input reads `GLFW_JOYSTICK_1` only; no multi-gamepad support yet.

## Known gaps / pitfalls

- Multi-gamepad support, analog stick axes (only digital button bindings
  exist), and action-map rebinding UI (the API supports it; no Inspector
  panel for it yet): deferred, not Foundation D acceptance items.
- **UNTESTED on native Windows/MSVC** — physics, input, animation, audio and the HUD have only run on
  Linux (gcc, clang+ASan/UBSan); the mingw target is cross-compiled and
  build-verified only, never executed on real Windows.

## Windows / MSVC build fix (PR #3 failed on Franklin's MSVC 14.51 build)

- Cause: `Window() = default;` (window.hpp) and `AudioSystem() = default;` (audio.hpp) were
  inline defaulted private constructors of dllexport classes holding `std::unique_ptr<Impl>`.
  MSVC instantiates the `unique_ptr` destructor (for exception cleanup) in every TU that sees
  the header, where `Impl` is incomplete (`C2027`/`C2338`, first hit in `input.cpp`). GCC/Clang/mingw
  only define the ctor if it is odr-used.
- Fix: both ctors are declared in the header and defined `= default` in window.cpp / audio.cpp
  (same pattern as 56d605a). Also silenced noisy C4251/C4275 in CMake for MSVC.
- Verified: gcc Debug, clang ASan/UBSan, mingw cross-build all green.
- **UNTESTED on native Windows/MSVC** — Franklin please re-run on Windows.

## Standalone export (Foundation D part 3)

- `scripts/export-game.sh [--mingw] [--project DIR] [--module FILE] OUT` produces a self-contained folder (player + engine lib + module + project). Run `OUT/arcane_player` (add `--frames 30 --hidden` for a smoke run); it finds `OUT/game` relative to the executable.
- `test_export_game` (Linux, gl label) exports the sample from the build tree and runs the exported player from another cwd. The mingw export builds and collects DLLs but is not run.
- UNTESTED on native Windows/MSVC: the export (MSVC would need `scripts/export-game.sh` under bash and the `GetModuleFileNameW` path discovery), and the mingw-exported folder has not been run under Wine/Windows.

## Tilemaps (Foundation E part 1)

- `Tileset` JSON asset + `Tilemap` component (layers, grid, `tile_at`/`set_tile`, cell<->world coords, JSON via a `layers` string property), batched culled rendering through `render_scene` (`TilesetCache`), optional static Box2D colliders from `solid_tiles` (merged runs), editor Tilemap panel + viewport paint/erase. Decision 31.
- Tests: `test_tilemap` (tileset JSON/UV, coords, scene round-trip, rect merging, collider stops a falling body), `test_editor_tilemap` (viewport->world, paint/erase, TilesetCache).
- Not covered by an automated test: the actual GL drawing of tiles and the editor paint UI (compile-checked only). UNTESTED on native Windows/MSVC.
