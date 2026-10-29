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

**2. What you can visibly see**
- AF6 prerequisite work adds a strict project-opening API for future tooling; it has no new editor screen or CLI command. `test_editor_project` checks that strict loading refuses an unknown legacy module property without changing the file.
- FE15 adds a top-down arena with solid walls, chasing enemy, health HUD and hit particles, plus editable ArenaRules properties in scene JSON and the Inspector.
- AudioSource playback is audible in visible editor Play and player sessions when a device opens; fallback is logged.
- Camera2D appears in the Inspector; Play and player use its follow, bounds, zoom, shake and pixel snapping. Its settings appear in saved scene JSON.
- Texture files with Unicode names can now be opened by the renderer on Windows; the regression test checks a Unicode filename.
- New docs: `docs/formats/canonical-json.md`, `project.md`, `scene.md`, plus (AF2) `tileset.md`, `clip.md`, `hud.md`, `input.md`; JSON Schemas in `schemas/`.
- AF2: Tilemap layers are now rows of ids in the scene file (not a `;`/`,` string); tileset/clip files and the project's HUD/input are `arcane.*` version 2; HUD elements all have ids and images keep an asset id.
- Golden migrated files under `tests/golden/` (what a v2 project and scene look like).
- Editor saves now write the canonical v2 format; old files still open.
- No new GUI and no CLI yet (the `arcane` CLI is AF4).

**3. Enough to merge**
- Minimum: Windows/MSVC configure + build + `ctest` green, play the top-down arena as above, review the FE15 CLI gap in `docs/slice_topdown_gaps.md`, and review the partial-job limitations. Review the FE13 CLI gap below as well.
- Optional: open + save both samples in the editor and run the player on the saved project.

**4. Known risks**
- AF5 is PARTIAL: AF3 and AF4 remain unimplemented; scene/Inspector edits still bypass commands and generic undo, coalescing, save-point tracking and coverage lint are absent. See `docs/editor-command-inventory.md`.
- KI1 is PARTIAL: the required shared command registry and CLI foundation are incomplete; impulse API, one-way platforms, clip reopening and player clip hot-reload remain unimplemented.
- FE15 is partial: the missing AF3/AF4 CLI prevents CLI authoring, validation, fixed-frame run and export. The sample has not been visually inspected.
- FE14 is partial: AF3/AF4 are absent, so shared CLI audio settings and diagnostics are unavailable. Real-device playback has not been manually verified.
- FE13 is partial: AF3 registry and AF4 CLI remain incomplete; camera CLI commands, validation and shared GUI command routing are unavailable. The incomplete AF3 work is preserved on the local WIP branch.
- **UNTESTED on native Windows/MSVC** (only a mingw cross-build ran; Wine is unavailable). Audio playback, some command-line/project paths, ImGui settings paths and error messages still use narrow path conversions and may fail or display incorrectly for non-ASCII paths.
- **UNTESTED on native Windows/MSVC** (only a mingw cross-build ran). Float formatting uses `std::to_chars` (MSVC and libstdc++ both support shortest round-trip); golden byte equality is the test to watch on MSVC.
- `ProjectSession::save` now writes via a temp file + rename; on Windows a locked target file would make a save fail (reported as an error, old file kept).
- Saving a legacy project from the editor rewrites it to v2 (older engine builds cannot read v2). Unknown component properties in legacy files are still dropped silently on an editor save (migration refuses them).

---

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

Registered canonical scene properties, fixed-tick following and deterministic shake, bounds and pixel snapping; see `docs/formats/scene.md`. AF3/AF4 are partial and absent: shared GUI commands, CLI creation/property/validation and CLI shake round-trip remain unimplemented. UNTESTED on native Windows/MSVC.

FE13 verification: GCC Debug 40/40, Clang ASan/UBSan 40/40, MinGW cross-build passed; native Windows/MSVC and visual camera behavior are untested.

## FE14 audio

Visible editor/player use miniaudio with a null fallback; hidden runs stay device-free. Streaming playback applies source gain, loop and bus gain. Tests exercise null selection and simulated fallback. CLI audio commands await AF3/AF4. **UNTESTED on native Windows/MSVC**, including audible playback.

## FE15 — top-down validation slice

Canonical v2 project and scene with a registered native module, zero-gravity actors, tilemap walls, direct enemy chase, health HUD and hit burst. `test_topdown_slice` covers strict text edit/round trip and 600 headless fixed steps; player/export smoke tests use a virtual display. See `docs/slice_topdown_gaps.md` for limitations. **UNTESTED on native Windows/MSVC.**

FE15 validation: GCC Debug 43/43 CTest, Clang ASan/UBSan 43/43 CTest, MinGW cross-build passed; no MinGW executables were run.

## KI1 — carry-over issues

No KI1 implementation was made: AF3 and AF4 are partial and absent, and the required shared command registry must be completed first. Physics impulse, one-way platforms, clip reopening and player clip hot-reload remain open. See the KI1 job handoff. **UNTESTED on native Windows/MSVC.**

## AF5 — prerequisite and mutation inventory (partial)

AF3 and AF4 remain incomplete. The editor mutation inventory records outstanding direct writes for AF5–AF8; no command routing or undo implementation is claimed. Native Windows/MSVC remains UNTESTED.

## AF6 prerequisite progress

ProjectSession and module-aware ProjectRuntime can opt into strict legacy scene loading. Their existing default remains compatible. The shared command registry, CLI, generic undo, and AF6 tilemap/tileset commands and editor routing are still missing; AF6 is PARTIAL. `scripts/build-and-test.sh` passed GCC Debug CTest, Clang ASan/UBSan CTest, and MinGW cross-build. UNTESTED on native Windows/MSVC.
