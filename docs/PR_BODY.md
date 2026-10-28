# PR #3 — Windows build fix, Foundation D (physics, input, animation, audio, HUD, export), Foundation E part 1 (tilemaps)

## Summary (PR #3 now targets `master`)

PR #3 was rebased onto `master` (e5591b2, which contains merged PR #2 /
Foundation C). It now contains, in order:

| Job | Scope | Status |
| --- | ----- | ------ |
| WINFIX | Fix for Franklin's MSVC 14.51 build failure (out-of-line `Window`/`AudioSystem` ctors). See "Windows / MSVC build fix" below. | Done (gcc, clang+san, mingw green; native MSVC UNTESTED by us) |
| FD1 | Box2D physics (`RigidBody2D`, `Collider2D`, `PhysicsWorld`), serializable input action map, Play-mode and player wiring, sample `PlayerController`. | Done |
| FD2 | Animation clips + render-time frame selection, null-backend audio (`AudioSource`, `play_on_start`), editor clip/sound assignment and animation preview, HUD (text, image, progress bar, anchor layout) + `HudRenderer`. Includes the previously uncommitted editor_core WIP (`7d1696f`), now verified and finished. | Done, with gaps listed under "Known gaps" |
| FD3 | `scripts/export-game.sh` standalone export; `arcane_player` finds `<exe dir>/game`. | Done (Linux run-tested; mingw export built, not run) |
| FE1 | Tilemaps: `Tileset` asset, `Tilemap` component, culled batched rendering, solid-tile static colliders, editor Tilemap panel + paint/erase. | Done (GL tile drawing and editor paint UI compile-checked only) |

Physics, input, animation, audio, HUD and tilemaps are all engine
features that are wired into both the editor (Play mode) and the
standalone player. Foundation D/E details follow.

### Foundation D — physics, input, animation, audio, HUD (FD1/FD2)

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
  between screen space (y-down) and Box2D's y-up world are both confined to that
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

## Test results (this PR, run under `xvfb-run -a`, via `scripts/build-and-test.sh`)

| Config | Result |
| ------ | ------ |
| gcc 14 Debug | **PASS** — 25/25 tests (4+ `gl`-labeled, run for real via Xvfb/Mesa llvmpipe) |
| clang 19 + `ARCANE_SANITIZE=ON` (ASan+UBSan) | **PASS** — 25/25 tests |
| mingw-w64 cross-compile | **PASS** (build only) — not run on real Windows |
| native MSVC 14.51 | **UNTESTED by us** (no MSVC on the build box); Franklin's last log predates the WINFIX commit |

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
- FE4 added `test_tilemap_gl` (offscreen FBO pixel checks: placement, layer order, camera-edge culling, zoom) and `test_tile_paint` (`TilePaintTool`: pan/zoom coords, brush/erase, fill-rect, undo/redo, dirty flag). The ImGui widgets around the tool remain compile-checked only. UNTESTED on native Windows/MSVC.

## Known issues and gaps (as of this PR's head)

- HUD: no buttons, focus or input routing; text is ASCII only (`stb_easy_font`).
- Audio: null backend only in editor and player (decision 27); no real device has been opened.
- Animation: clip cache does not hot-reload; the Assets panel image list re-scans per frame, clips/sounds only at startup (decision 28).
- Input: digital bindings only (no analog axes), single gamepad (`GLFW_JOYSTICK_1`), no rebinding UI.
- Tilemaps: no tileset editor, auto-tiling, or GL test for tile drawing; the editor paint UI is compile-checked only.
- Export: no installer/packaging; mingw output is built but never run (not under Wine or Windows).
- Physics: decision 3 says the screen/Box2D y-flip is confined to `PhysicsWorld`, but the code does not apply one (positions pass through unchanged, and row 31 says so). Gravity direction on screen is therefore unverified. Needs a decision and a test before anyone relies on it. Not fixed in this wrap-up.

## Next steps

- Franklin: re-run the Windows MSVC build on this PR's head and send the log; that is the only way to close the UNTESTED items below.
- Add a GL test for tile drawing and a headless test for the editor paint tool.
- Rebinding UI for the input action map; HUD buttons and input routing.
- Consider the mingw export under Wine as an optional smoke test (not in CI; no CI files are allowed in this repo).

## UNTESTED on native Windows/MSVC

Everything below has been built or cross-compiled (mingw) and, where it
has tests, run only on Linux (gcc and clang+ASan/UBSan under Xvfb):

- Windows/MSVC build fix (WINFIX) — compile only verified on the Linux toolchains and mingw.
- Physics (Box2D world, contacts, Play/Stop isolation with physics).
- Input action map with the GLFW backend (real device input).
- Animation clips and render-time frame selection.
- Audio null backend (the null backend itself is headless, but the MSVC build of `miniaudio` is not checked).
- HUD text, image, progress bar and layout rendering.
- Standalone export (`scripts/export-game.sh`), exe-relative project discovery via `GetModuleFileNameW`, and the mingw export folder (never run).
- Tilemaps: the component, colliders, culled rendering, and the editor Tilemap panel and paint tool.

## Delegated decisions (PR #3 as a whole)

Decisions with no explicit instruction, all logged in `docs/decisions.md`:
rows 3, 24–31 (physics scope and units, input action map storage and
live binding, input backend abstraction, null audio backend, editor
animation preview, HUD text and layout, export layout, tilemap format),
plus the MSVC pimpl out-of-line constructor pattern (WINFIX section below).

## FE8 — input maps

Input actions support four gamepads, player routing and analog axes with a rescaled deadzone and button threshold. Runtime rebinding listens for new input and detects conflicts; the editor Input Map panel edits the project map. Existing version 1 project input maps remain readable. Headless tests cover axis math, player routing, rebinding and project serialization. The panel edits numeric device codes; its widgets and physical devices have not been manually exercised.

**UNTESTED on native Windows/MSVC** — FE8 input, GLFW gamepad polling and editor Input Map panel; the mingw build is cross-compiled only.

FE9 adds tileset authoring and four-neighbor terrain painting with undo. UNTESTED on native Windows/MSVC; editor interaction requires manual verification.
