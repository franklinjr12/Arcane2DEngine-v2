## Franklin: how to test / validate

**1. Step-by-step manual checks**

Linux:
```
scripts/build-and-test.sh          # gcc Debug + clang ASan/UBSan + mingw cross-build; expect gcc PASS and clang PASS
ctest --test-dir .build-and-test/build-gcc -R "canonical|migration|scene_json|formats_v2|camera2d"   # expect all passed
xvfb-run -a ctest --test-dir .build-and-test/build-gcc -R topdown --output-on-failure
.build-and-test/build-gcc/bin/arcane_editor --project samples/topdown_slice
.build-and-test/build-gcc/bin/arcane_player --project samples/topdown_slice
.build-and-test/build-gcc/bin/arcane_editor --project samples/sample_project
.build-and-test/build-gcc/bin/arcane_player --project samples/sample_project
```
The editor opens the sample project; select a node, add Camera2D in the Inspector, and set its target node ID, zoom, bounds, or shake values. Save, click Play, and observe the camera follow and shake; click Stop to return to editing. The player opens the saved scene. Existing samples continue to load.
For FE15, open the arena in the editor, select Arena and inspect the registered ArenaRules values; click Play and use WASD. The blue player moves in four directions and stops at the perimeter; the red enemy follows and contact lowers the health HUD and creates a burst. Stop returns to editing. The player command opens the same arena. Run `ctest --test-dir .build-and-test/build-gcc -R "topdown" --output-on-failure` under `xvfb-run -a` for the 600-step simulation, 120-frame player and packaged export checks.

Windows / MSVC (PowerShell, from the repo root):
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: all tests pass, in particular `test_canonical_json` and `test_migration` (the latter migrates
both sample projects in a temp copy and compares against `tests/golden/*`).

AF2 check: the same `test_migration` now also migrates the sample tileset/clip files and nested HUD/input
(`tests/golden/*/assets/*.json`, `tests/golden/*/project.json`); `test_formats_v2` covers clips, tilesets
(FE9 terrain), tile rows, HUD ids/images, input defaults and unsupported versions.

Open a sample in the editor and save it (File > Save): `samples/sample_project/project.json` and the
scene now start with `"format": "arcane.project"` / `"arcane.scene"` and `"version": 2`, keys sorted,
two-space indent. Reopening it works; use `git diff` to see the migrated text, `git checkout samples`
to restore the legacy copies.

To launch the existing applications on Windows after the build:
```
.\build\bin\arcane_editor.exe --project samples/topdown_slice
.\build\bin\arcane_player.exe --project samples/topdown_slice
.\build\bin\arcane_editor.exe --project samples/sample_project
.\build\bin\arcane_player.exe --project samples/sample_project
```
Open the sample project in the editor, add Camera2D to a node via the Inspector, set a target ID and shake settings, and use File > Save. Click Play and observe follow and shake; Stop returns to editing. Open the saved project in the player and observe the same camera. The `arcane` CLI is not available in this branch yet, so a CLI check cannot be run.

FE12B: the `test_renderer_gl` test also creates and loads a texture with a Unicode filename; expect it to pass.

Audio check (Linux or Windows): select a node in the editor, add `AudioSource`, assign a WAV from Assets, enable `play_on_start`, and click Play. With an available playback device, expect sound; set `loop` to hear it repeat and adjust `volume` to change its level. Stop ends playback. Run the player on the saved project to hear the same source. Without a device the application logs a warning and continues silently. Hidden runs use the null backend. The `arcane` CLI does not exist on this branch; a CLI command cannot be run.

AF9 prerequisite check: open a temporary copy of a sample in the editor, change its scene file externally, then make an editor edit and use File > Save. Expect a save error and the external file to remain unchanged. Reopen the project before saving again. No `arcane` CLI exists yet, so CLI checks cannot be run.

AF3 check: `ctest --test-dir .build-and-test/build-gcc -R test_tooling_commands --output-on-failure` (Windows: `ctest --test-dir build -C Debug -R test_tooling_commands --output-on-failure`); expect it to pass. It sets and undoes typed properties on `sample_game`'s `Spinner` and on builtin `Camera2D`/`Sprite` in a temporary project; there is nothing to click or run by hand.

FE18 check (prefabs): `ctest --test-dir .build-and-test/build-gcc -R test_prefab --output-on-failure` (Windows: `ctest --test-dir build -C Debug -R test_prefab --output-on-failure`); expect it to pass. It saves a node subtree as `prefabs/rig.prefab.json` in a temporary project, instantiates it three ways through the registry commands, overrides/reverts properties, breaks and restores the link, reopens the project from disk, spawns 25 runtime instances and checks that malformed links are refused. There is no editor button or `arcane` CLI for prefabs (see risks), so nothing to click.

FE20 check (atlas packing): `ctest --test-dir .build-and-test/build-gcc -R test_atlas --output-on-failure` (Windows: `ctest --test-dir build -C Debug -R test_atlas --output-on-failure`); expect it to pass. It packs generated PNGs through `atlas.pack`, checks no overlaps/padding/bounds, byte-identical repeat output, the metadata round trip, UV lookup and a clip built from regions. No GUI or `arcane` CLI exists for it, so nothing to click.

FE21 check (tween/timers): `ctest --test-dir .build-and-test/build-gcc -R test_tween --output-on-failure` (Windows: `ctest --test-dir build -C Debug -R test_tween --output-on-failure`); expect it to pass. It checks easing values, delay/loop/yoyo timing, completion on the 60th fixed step, cancellation when a node is destroyed, timers, and save/load/play of an authored `Tween` component. In the editor you can add a `Tween` component in the Inspector (target `position.x`, `to`, `duration`, `easing`) and click Play to see the node move; there is no `arcane` CLI, and `prop.set` on `Tween` properties is the only command path.

AF10: no `arcane run`, `test`, or `export` command is available yet; there is no AF10 manual CLI check to run.

**2. What you can visibly see**
- AF3 adds the `arcane_tooling` library and `docs/commands.md` (command reference, error codes); no new GUI or CLI executable. Visible only through `test_tooling_commands`.
- AF11 adds `AGENTS.md` with build, format and current authoring guidance, and expands the editor action inventory. No CLI or generated command reference is available.
- AF9 prerequisite: saving reports external edits detected in the project or scene since opening or the last save.
- AF8 prerequisite: opening legacy data remains compatible, but Save reports an error if the originally opened project or scene contains unknown data that would be lost.
- AF7 adds no visible feature: clip, HUD and input commands and panel routing remain blocked by incomplete command and undo foundations.
- AF6 prerequisite work adds a strict project-opening API for future tooling; it has no new editor screen or CLI command. `test_editor_project` checks that strict loading refuses an unknown legacy module property without changing the file.
- FE15 adds a top-down arena with solid walls, chasing enemy, health HUD and hit particles, plus editable ArenaRules properties in scene JSON and the Inspector.
- AudioSource playback is audible in visible editor Play and player sessions when a device opens; fallback is logged.
- Camera2D appears in the Inspector; Play and player use its follow, bounds, zoom, shake and pixel snapping. Its settings appear in saved scene JSON.
- Texture files with Unicode names can now be opened by the renderer on Windows; the regression test checks a Unicode filename.
- New docs: `docs/formats/canonical-json.md`, `project.md`, `scene.md`, plus (AF2) `tileset.md`, `clip.md`, `hud.md`, `input.md`; JSON Schemas in `schemas/`.
- AF2: Tilemap layers are now rows of ids in the scene file (not a `;`/`,` string); tileset/clip files and the project's HUD/input are `arcane.*` version 2; HUD elements all have ids and images keep an asset id.
- Golden migrated files under `tests/golden/` (what a v2 project and scene look like).
- Editor saves now write the canonical v2 format; old files still open.
- FE16 adds `docs/acceptance-audit.md` (baseline section 17 and AI-agent-friendly criteria mapped to tests or GAP) and `test_foundation_acceptance` (Sprite + Camera + module Spinner saved, reopened, played, stopped).
- FE18 adds prefab files (`docs/formats/prefab.md`, `schemas/arcane.prefab.schema.json`), a `prefab` link on scene nodes, `prefab.*` registry commands (`docs/commands.md`) and the runtime calls `load_prefab_file` / `instantiate_prefab` / `spawn_prefab`. Visible only as files and test output.
- FE20 adds `docs/formats/atlas.md`, `schemas/arcane.atlas.schema.json`, `arcane/atlas.hpp` (packer, load/save, UV lookup) and `atlas.pack/regions/make_clip` registry commands. Visible as files and test output only.
- No new GUI and no CLI yet (the `arcane` CLI is AF4).

**3. Enough to merge**
- Minimum: Windows/MSVC configure + build + `ctest` green, play the top-down arena as above, review the FE15 CLI gap in `docs/slice_topdown_gaps.md`, and review the partial-job limitations. Review the FE13 CLI gap below as well.
- Optional: open + save both samples in the editor and run the player on the saved project.

**4. Known risks**
- FE20 is PARTIAL: packing, format, commands and tests are done; there is no `arcane` CLI (AF4) so `atlas.*` is reachable from C++/tests only, no editor UI, and `Sprite` cannot reference a region by name (clips can, via `atlas.make_clip`). Export was not run end to end with an atlas (it copies `assets/` whole). **UNTESTED on native Windows/MSVC.**
- FE18 is PARTIAL: data format, registry commands, runtime spawn and tests are done; the editor GUI has no prefab actions (editor edits still bypass commands, AF5) and there is no `arcane` CLI (AF4). Prefab edits do not propagate to existing instances; undoing a `prefab.remove` or redoing an `instantiate` creates new IDs. **UNTESTED on native Windows/MSVC.**
- AF3 is PASS for its scope (registry, context, property commands) but nothing calls it yet: the editor does not route through it (AF5) and the `arcane` CLI (AF4) does not exist. Only property commands exist. Persistence writes the scene only and is optimistic about concurrent writers. **UNTESTED on native Windows/MSVC** (`tooling/` uses `std::expected`, `std::to_chars(float)`, `std::filesystem`; `ProjectContext` has an out-of-line destructor).
- AF11 is PARTIAL: AF3–AF10 prerequisites remain incomplete, so generated CLI documentation, registry coverage and drift gates, schema-example validation and the end-to-end CLI workflow were not delivered.
- AF10 is PARTIAL and has no implementation: the shared command registry and CLI prerequisites are incomplete, so display-free fixed-frame run, project test and structured CLI export are unavailable. The existing export script still rewrites the module field with `sed`; the player still requires a display.
- AF9 is PARTIAL: batch, asset import/reimport, recovery and CLI remain unavailable because AF3–AF8 prerequisites are incomplete. The save revision check is optimistic: a concurrent writer can race after the check, and project/scene saves are not transactional.
- AF8 is PARTIAL: the shared registry and CLI, AF3–AF7 prerequisites, AF8 property/project commands, editor routing, undo and command-coverage enforcement remain unimplemented. The save guard and revision check do not make project and scene writes transactional.
- AF7 is PARTIAL: clip reopening, clip/HUD/input commands, HUD image editing through commands, and panel undo/routing are not implemented. AF3, AF4 and AF5 remain prerequisites; the preserved AF3 WIP is not safe to integrate unchanged.
- AF5 is PARTIAL: AF3 and AF4 remain unimplemented; scene/Inspector edits still bypass commands and generic undo, coalescing, save-point tracking and coverage lint are absent. See `docs/editor-command-inventory.md`.
- KI1 is PARTIAL: the required shared command registry and CLI foundation are incomplete; impulse API, one-way platforms, clip reopening and player clip hot-reload remain unimplemented.
- FE15 is partial: the missing `arcane` executable (registry `prop.*` now covers ArenaRules properties in C++ via `test_topdown_commands`) prevents CLI authoring, validation, fixed-frame run and export. The sample has not been visually inspected.
- FE14 is partial: the `arcane` CLI still does not exist, so audio settings (`prop.set` on `AudioSource`) and `audio.backend` diagnostics are registry commands reachable from C++/tests only. Real-device playback has not been manually verified.
- FE13 is partial: AF3 registry and AF4 CLI remain incomplete; camera CLI commands, validation and shared GUI command routing are unavailable. The incomplete AF3 work is preserved on the local WIP branch.
- FE16 is PARTIAL: the audit finds the CLI-based criteria (1, 3, 4 parity, 5) unmet because AF3 CLI/AF4-AF11 are incomplete; section 17 is met on Linux with Windows/MSVC UNTESTED. See `docs/acceptance-audit.md`.
- **UNTESTED on native Windows/MSVC** (only a mingw cross-build ran; Wine is unavailable). Audio playback, some command-line/project paths, ImGui settings paths and error messages still use narrow path conversions and may fail or display incorrectly for non-ASCII paths.
- **UNTESTED on native Windows/MSVC** (only a mingw cross-build ran). Float formatting uses `std::to_chars` (MSVC and libstdc++ both support shortest round-trip); golden byte equality is the test to watch on MSVC.
- `ProjectSession::save` now writes via a temp file + rename; on Windows a locked target file would make a save fail (reported as an error, old file kept).
- Saving a compatible legacy project from the editor rewrites it to v2 (older engine builds cannot read v2). Save now refuses unknown fields detected in the project and scene at open; it detects ordinary external changes before saving but cannot prevent a race after its check.

---

## Overnight wrap (2026-10-29 02:40 BRT)
- Branch `engine-nightly-2026-10-28` HEAD `e464cfe`: gcc Debug build and CTest pass, 43/43 (headless and under `xvfb-run`). Clang ASan/UBSan and mingw were not re-run in this wrap; their last full results are in the job notes above.
- FE18 prefab WIP from that night was completed on `engine-nightly-2026-10-29` (see FE18 section).
- The older `engine-nightly-2026-10-28-wip` branch (tip `613fadb`, preserved AF3 tooling) was left untouched.
- Status: AF1, AF2, FE1-FE5, FE7-FE12B, FD1-FD3 and WINFIX PASS. AF3-AF11, FE13-FE15, FE17 and KI1 are PARTIAL or not complete. FE18 is PARTIAL (WIP branch only). FE20-FE23, AF12 and FE16 were not started.
- Native Windows/MSVC remains UNTESTED (mingw cross-build only).

## AF11 — documentation and inventory (partial)

Added root agent guidance and reviewed editor actions. Registry-generated CLI reference and coverage gates, schema-example validation and the CLI workflow remain blocked by missing prerequisites. **UNTESTED on native Windows/MSVC.**

# Engine nightly 2026-10-28 — AF1 (draft)

**Base:** master 580b0bf (PR #3 and #4 merged). **Head:** `engine-nightly-2026-10-28`.

## AF1 — canonical project and scene compatibility
- `arcane/canonical_json.hpp`: `canonicalize_json`, `read_text_file`, atomic/staged file writes; internal strict SAX parser (duplicate keys rejected) and canonical writer (`engine/src/canonical_json.*`).
- Scene and project writers emit canonical v2 (`arcane.scene` / `arcane.project`, `version: 2`); readers accept v2 (strict) and legacy v1 (lenient as before). Stricter value typing everywhere (no silent `1.5`→int, no uncaught exceptions on bad types).
- `arcane/migration.hpp`: `migrate_project_json`, `migrate_scene_json`, `migrate_project` (dry run, staged commit, rollback, idempotence verified on every result).
- `ProjectSession::save` serializes before writing and uses atomic writes.
- Docs `docs/formats/*`, schemas `schemas/*.schema.json`; decisions 51–60.
- Tests: `test_canonical_json` (format rules, float policy, malformed/future/unknown), `test_migration` (both samples with their modules: semantic equality, byte-identical repeat, golden files, unknown property/field, future version, malformed, rollback on commit failure, missing module types).

## AF2 — tileset, clip, HUD, input and tile-row migration
- `arcane.tileset` / `arcane.clip` v2 writers and strict readers (legacy v1 kept), `migrate_tileset_json` / `migrate_clip_json`.
- Nested `arcane.hud` / `arcane.input` v2; HUD elements get stable string ids (assigned deterministically for legacy files), `HudImage::texture_asset` is persisted; strict range/duplicate checks for input.
- Scene v2 `Tilemap.layers` is structured rows; old text form still reads and migrates (strictly).
- `migrate_project` also migrates tileset/clip files (found by content, never renamed).
- Schemas for the four new kinds, docs/formats, decisions 61–68, tests `test_formats_v2` + extended `test_migration` with goldens.
- Not done (later jobs): resolving `HudImage::texture_asset` to a texture at Play (AF7); `solid_tiles` stays a string.

## FE17 — one-way platforms

`Collider2D.one_way` (static box; land from above, pass from below) and `PlatformerController2D.down_action` / `drop_duration` (hold down + jump to drop through). Canonical JSON gains `one_way`, `down_action`, `drop_duration` (golden scenes updated, additions only). Headless test `test_platformer` covers landing, drop-through and passing from below. Manual check: add a Collider2D to a static body in the editor, tick `one_way`, Play, jump up through it, land, press S+Space. `one_way`, `down_action` and `drop_duration` are settable via `prop.set` (round-trip tested in `test_tooling_commands`; documented in `docs/formats/scene.md`). Not done: tile-flag one-way, slopes, the `arcane` CLI binary (does not exist). **UNTESTED on native Windows/MSVC.**

## Test results
`scripts/build-and-test.sh`: gcc Debug PASS (39/39 tests; AF2 adds test_formats_v2), clang ASan/UBSan PASS (39/39), mingw-w64 cross-build PASS (build only, not run).

## UNTESTED on native Windows/MSVC
Everything above (mingw cross-build only), notably `std::to_chars` float formatting, `std::filesystem::rename` over an existing file, and the golden-file byte comparison.

## AF4 prerequisite review
AF3 is PARTIAL: its preserved WIP is not integrated and lacks property commands and tests. Its save path must be corrected to avoid partial multi-file writes and strict legacy loading must be enforced before authoring. AF4 has not been implemented. The existing branch was revalidated with `scripts/build-and-test.sh`: gcc and clang sanitizer tests passed, and the mingw cross-build passed.

## FE12B — Windows hardening audit
- Audited public headers, DLL exports, pimpl special members, non-ASCII literals, narrow paths and compiler-sensitive conversions. The four engine pimpl owners define their constructors, destructors and moves out of line; editor core is statically linked. Public non-inline engine APIs examined are exported. Shared GLFW/glad setup is unchanged.
- Texture file loading opens native Windows paths with `_wfopen`, then passes the opened file to stb; `test_renderer_gl` loads a texture whose filename contains escaped Unicode characters.
- `scripts/build-and-test.sh`: GCC Debug 39/39 PASS, Clang ASan/UBSan 39/39 PASS, MinGW cross-build PASS. No Wine installed; Windows executables were not run. **UNTESTED on native Windows/MSVC**. Narrow path handling remains in audio, some app paths and diagnostics, so this does not establish general Unicode path support.


## FE13 Camera2D

Registered canonical scene properties, fixed-tick following and deterministic shake, bounds and pixel snapping; see `docs/formats/scene.md`. The AF3 registry is now present: every Camera2D setting is settable via `prop.set` (validated, persisted) and `test_tooling_commands` round-trips them, including a seeded shake that replays identically after reopening. Still missing: the `arcane` CLI (AF4), node/component creation commands and Inspector routing through commands. UNTESTED on native Windows/MSVC.

FE13 verification (re-run with the command test): GCC Debug and Clang ASan/UBSan all passed, MinGW cross-build passed; native Windows/MSVC and visual camera behavior are untested.

## FE14 audio

Visible editor/player use miniaudio with a null fallback; hidden runs stay device-free. Streaming playback applies source gain, loop and bus gain. Tests exercise null selection and simulated fallback. Registry command `audio.backend` reports the selected backend (no device opened unless `probe_device=true`) and AudioSource settings are covered by `prop.set` tests; the `arcane` CLI wrapper does not exist yet. **UNTESTED on native Windows/MSVC**, including audible playback.

## FE15 — top-down validation slice

Canonical v2 project and scene with a registered native module, zero-gravity actors, tilemap walls, direct enemy chase, health HUD and hit burst. `test_topdown_slice` covers strict text edit/round trip and 600 headless fixed steps; player/export smoke tests use a virtual display. `test_topdown_commands` lists/sets the registered `ArenaRules` properties through the shared registry (persist, dry run, rejection). See `docs/slice_topdown_gaps.md` for limitations. **UNTESTED on native Windows/MSVC.**

FE15 validation: GCC Debug 43/43 CTest, Clang ASan/UBSan 43/43 CTest, MinGW cross-build passed; no MinGW executables were run.

## KI1 — carry-over issues

No KI1 implementation was made: AF3 and AF4 are partial and absent, and the required shared command registry must be completed first. Physics impulse, one-way platforms, clip reopening and player clip hot-reload remain open. See the KI1 job handoff. **UNTESTED on native Windows/MSVC.**

## AF5 — prerequisite and mutation inventory (partial)

AF3 and AF4 remain incomplete. The editor mutation inventory records outstanding direct writes for AF5–AF8; no command routing or undo implementation is claimed. Native Windows/MSVC remains UNTESTED.

## AF6 prerequisite progress

ProjectSession and module-aware ProjectRuntime can opt into strict legacy scene loading. Their existing default remains compatible. The shared command registry, CLI, generic undo, and AF6 tilemap/tileset commands and editor routing are still missing; AF6 is PARTIAL. `scripts/build-and-test.sh` passed GCC Debug CTest, Clang ASan/UBSan CTest, and MinGW cross-build. UNTESTED on native Windows/MSVC.

## AF8 prerequisite progress (partial)

Compatible-loaded ProjectSession saves refuse unknown legacy project or scene data instead of silently discarding it. Headless tests exercise a real module property and a project field, preserving the original file and dirty state. The editor action inventory now classifies more widgets, but registry/CLI coverage and enforcement remain absent. `scripts/build-and-test.sh`: GCC Debug and Clang ASan/UBSan CTest passed; MinGW cross-build passed. **UNTESTED on native Windows/MSVC.**

## AF9 prerequisite progress (partial)

ProjectSession checks the bytes of both inputs before saving and rejects detected external changes, leaving the dirty flag set. This is not a lock and does not provide transactional multi-file writes. AF3–AF8 and AF9 command/CLI acceptance remain incomplete. **UNTESTED on native Windows/MSVC.**

## FE16 — foundation acceptance audit

`docs/acceptance-audit.md` lists each baseline section 17 bullet and AI-agent-friendly criterion with test evidence or GAP. Gap fixed: `test_foundation_acceptance`. Big gaps (CLI, command routing, display-free run, schema validation tests, Windows run) are listed as next jobs there.

## FE18 — prefabs

- Format: `arcane.prefab` v1 (one-root scene subtree, local IDs) and a `prefab` link on every instance node (`asset`, `instance_id`, `local_id`; explicit `overrides` list on the instance root). Old scenes load unchanged, no migration. Docs `docs/formats/prefab.md`, `scene.md`; schemas `arcane.prefab`, `arcane.scene` (additive).
- Engine: `arcane/prefab.hpp` (`Prefab`, `instantiate_prefab`, `spawn_prefab`, override/revert/break). `Color` gained `operator==`. EntityId properties are remapped inside a copy and cleared when pointing outside.
- Commands (`arcane_tooling`): `prefab.save/instantiate/remove/override/revert/break_link/relink/info`, with inverses; `property_to_json`/`property_from_json` shared with `prop.*`.
- Tests: `test_prefab` (save/instantiate/override round trip through disk, ID uniqueness, reference remapping, break/relink, 25 runtime spawns, malformed link refusal, dry run).
- Gaps: no GUI (AF5) or `arcane` CLI (AF4) spelling, no propagation of prefab edits, no nesting. **UNTESTED on native Windows/MSVC.**

## FE20 — sprite atlas packing

- Format `arcane.atlas` v2 (pixel rects, derived UVs), deterministic shelf packer over `*.png` folders using stb_image/stb_image_write, PNG output byte-stable. Docs `docs/formats/atlas.md`, `commands.md`; schema `arcane.atlas`.
- Commands: `atlas.pack`, `atlas.regions`, `atlas.make_clip` (`tooling/src/atlas_commands.cpp`, `register_atlas_commands`).
- Tests: `test_atlas` (layout invariants over 40 sizes x 3 paddings, input-order independence, bounds/overlap, JSON strictness and round trip, folder packing, repeat-run bytes, clip UVs, argument refusal). Decision logged in `docs/decisions.md`.

## FE21 — tween/easing and timers

- `engine/include/arcane/tween.hpp`: 12 easing curves (`ease`, stable text names), pure `evaluate_tween` (delay, loops, yoyo), `TweenSystem` (free-standing tweens with apply/on-complete callbacks, node-property tweens, cancel by id or node), `Timers` (`after`/`every`, optionally node-bound) and the registered `Tween` component (docs/formats/scene.md).
- Driven by the fixed update (caller passes the fixed dt); tweens/timers bound to a node are dropped silently when the node is destroyed or pending destruction. Decision logged in `docs/decisions.md`.
- Gaps: HUD elements are not nodes, so HUD tweens use the free-standing `start()` with a lambda (no authored HUD tween); no `tween.*` CLI/GUI commands (AF4/AF5 absent); `Tween` target/easing are strings (typos make it inert). **UNTESTED on native Windows/MSVC.**
