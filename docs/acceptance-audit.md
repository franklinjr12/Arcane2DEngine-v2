# Foundation acceptance audit (FE16)

Audit of baseline section 17 and the AI-agent-friendly acceptance criteria
(plan section 1) against branch `engine-nightly-2026-10-28`. Evidence is a
test name (run `ctest --test-dir <build> -R <name>`; GL-labelled tests need a
display or Xvfb) or a command. **GAP** means not met. All results are Linux
(gcc Debug, clang ASan/UBSan) plus a MinGW build-only cross-compile;
**native Windows/MSVC is UNTESTED**.

## Baseline section 17

| Bullet | Status | Evidence / gap |
| --- | --- | --- |
| Clean checkout builds the C++23 engine shared library and editor on Windows and Linux with documented commands | Linux PASS; Windows UNTESTED | `scripts/build-and-test.sh` (gcc, clang-san, MinGW build); README "Build and test". Windows/MSVC commands are documented in AGENTS.md and the README but were never run natively (MinGW is only a cross-build). |
| Example game module builds per OS, registers a custom behavior/property, used by editor and player | Linux PASS; Windows UNTESTED | `test_module_registration` (Spinner descriptor get/set, save/reload), `test_module_load`, `test_editor_frames` and `test_player_project_frames` (GL, load `sample_game` in editor and player). MinGW builds the module, never loaded on Windows. |
| Editor opens a project, shows hierarchy and preview, edits a serializable property | Partial | Property editing through the registry: `test_editor_project` (registry walk incl. module type), `test_tooling_commands`. Opening and drawing the hierarchy/preview is only a smoke run: `test_editor_frames` (30 hidden frames). No test clicks the UI; GAP: no automated check of what the panels show. |
| Scene with sprite, camera, custom component saved, closed, reopened, played | PASS (added by FE16) | `test_foundation_acceptance`: Sprite + Camera + Spinner saved with `ProjectSession`, session closed, reopened, played 60 ticks. Note the committed `samples/sample_project` scene has no module component; that gap is covered by this test, not by the sample. |
| Play/Stop does not replace authored values | PASS | `test_editor_state` (isolation, with physics), `test_foundation_acceptance` (module on_update runs on the runtime copy, authored JSON byte-identical after Stop, session not dirty). |
| Standalone executable launches the sample project without the editor | PASS on Linux | `test_export_game`, `test_export_platformer_slice`, `test_export_topdown_slice` (export to a temp dir, run `arcane_player` from another cwd), `test_player_*frames`. UNTESTED on Windows (DLL placement). |
| Pure scene, component, serialization, timing tests run headless on Linux | PASS | `test_scene`, `test_scene_json`, `test_canonical_json`, `test_formats_v2`, `test_timestep`, `test_input`, `test_animation`, `test_tilemap`, etc. need no window or audio (see the headless list in `tests/CMakeLists.txt`). |
| Ownership/lifecycle tests pass with no known leaks | PASS for covered paths | clang ASan/UBSan run of the full CTest suite is green (`build-and-test.sh`); `test_scene` (lifecycle order, destroy subtree, ID reuse), `test_tooling_commands` (`test_context_lifetime_and_registry_listing`). GL tests under sanitizers are limited by llvmpipe (see `tests/CMakeLists.txt`). GAP: no test dlcloses a module while instances live; modules are never unloaded while running (decision 8). |
| Readability policy | Not mechanically checked | `-Wall -Wextra -Wpedantic` (`/W4` on MSVC); no template-heavy code (4 `template` mentions in public headers). Review item, not a test. |

## AI-agent-friendly acceptance (plan section 1)

| Criterion | Status | Evidence / gap |
| --- | --- | --- |
| 1. Every editor data action is a registered, documented CLI command incl. module properties; view-only state classified | **GAP (major)** | Registry exists (`tooling/`, `docs/commands.md`) with `prop.list/get/set` and `audio.backend`, tested by `test_tooling_commands`, `test_topdown_commands` (module `Spinner`/`ArenaRules`). No `arcane` CLI. Hierarchy, tile paint, tileset, clip, HUD, input and asset-assignment edits still bypass commands (`docs/editor-command-inventory.md`, AF5-AF8 PARTIAL). View-only state is classified in that inventory only, with no lint. |
| 2. Resources are documented UTF-8 text; validation reports file and JSON pointer; stable IDs and canonical output; samples load and migrate | Mostly PASS | `docs/formats/*.md`, `schemas/*.json`; `test_canonical_json`, `test_formats_v2` (clip, tileset, tile rows, HUD, input, nested project), `test_migration` (both samples vs `tests/golden/`, legacy loading, unknown data rejected). GAPs: no test validates files against `schemas/`; error reports name file and pointer only for some paths (not audited exhaustively); `samples/topdown_slice` is not in the golden set. |
| 3. CLI-only end-to-end project creation, validate, format twice, run frames, export, reopen | **PARTIAL** | AF10B adds `arcane run`, `arcane test` and `arcane export`, and `arcane_player --headless` (display-free fixed frames). `test_cli_e2e` covers the CLI path. AF10C adds `arcane render` (offscreen GL PNG with golden compare, exit 8 without GL; `test_render_cli`, `test_render_cli_nodisplay`). Still missing: AF5-AF8 routing for editor edits. Native Windows is untested. |
| 4. GUI and CLI produce equivalent model and bytes; coverage and doc-drift tests; module load failure and unknown properties never destroy data | Partial | Unknown data: `test_editor_project` (`test_compatible_project_unknown_fields_cannot_be_saved`, `test_strict_runtime_rejects_unknown_legacy_module_property`, `test_save_rejects_external_edits`), `test_open_failures_write_nothing`. AF11B: generated `docs/cli-reference.md` with a drift test (`test_cli`), a `docs/commands.md` coverage gate in both directions (`test_cli` `test_commands_doc_coverage`), schema-example validation of committed examples (`test_schema_examples`), the end-to-end CLI workflow (`test_cli_e2e`) and GUI/CLI parity for scene, tilemap, HUD, input and project edits (`test_edit_session`). GAPS: tileset and clip panel draft editing and Open clip stay direct (`docs/editor-command-inventory.md`); the lint in `test_edit_session` finds only known patterns; native Windows/MSVC untested. |
| 5. Deterministic errors, exit status, dry-run, batch; failed writes keep prior files; Windows-compatible paths | Partial | Error codes, dry-run and rollback: `test_failures_change_nothing`, `test_dry_run_and_conflict_roll_back`; atomic replace on save. Batch/JSONL (`arcane batch`, one staged transaction) and the multi-file project+scene save with failure injection now exist, as do `asset.import/reimport` with failure tests (`test_cli`, `test_asset_commands`); not crash-atomic. Native MSVC unverified. |

## Gaps fixed in FE16

- Added `tests/test_foundation_acceptance.cpp` (sprite + camera + module
  component save/close/reopen/play/stop).

## Next jobs (big gaps, not started)

1. Finish AF3/AF4: `arcane` CLI over the registry (exit codes, dry-run, JSONL batch).
2. AF5-AF8: route every editor data mutation through commands; add tile,
   tileset, clip, HUD, input and project commands plus GUI/CLI parity tests.
3. AF10: DONE (AF10B `run`, `test`, `export`, headless player; AF10C `render`).
4. AF11: DONE in AF11B (`docs/cli-reference.md` + drift test, `docs/commands.md` coverage gate, `test_schema_examples`, `test_cli_e2e`, `test_edit_session` parity). Remaining: `render`, tileset/clip draft routing, native Windows.
5. Native Windows/MSVC build and test run.
6. Add a module component to a committed sample scene, and `topdown_slice`
   to the golden migration set.
