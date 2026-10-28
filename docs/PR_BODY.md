# Engine nightly 2026-10-27 run 2 — FE2..FE12 (draft, stacked on PR #3)

**Base:** `engine-nightly-2026-10-26` (PR #3, frozen for the native Windows test). **Head:** `engine-nightly-2026-10-27`. Draft.

## Summary

Run 2 of the overnight engine work, on top of PR #3 (WINFIX, FD1–FD3, FE1). Commits: `ff2cdf2` … `53759b9`.

- **FE2 — physics y-flip.** Screen space is y-down; `PhysicsWorld` now flips y for positions, collider offsets, velocities and rotation sign (tilemap bodies included). Sample jump sign updated. Decisions 31–32.
- **FE3 — HUD buttons and input routing.** `HudButton` joins the HUD variant; `HudInput` hit-tests, handles focus navigation, click callbacks and input consumption (`MaskedInputBackend`). HUD lives in `project.json` under `"hud"`; editor HUD panel. Decisions 33–37.
- **FE4 — tile paint tool.** ImGui-free `TilePaintTool` (brush, erase, clamped fill-rect, stroke-based undo/redo). Editor uses it. Decision 39.
- **FE5 — PlatformerController2D.** Coyote time, jump buffer, jump cut, fall gravity; `RigidBody2D::grounded` from contact normals. One-way platforms not done. Decision 40.
- **FE6 — ParticleEmitter.** CPU fixed-step simulation, deterministic seed, batched draw, editor preview toggle. Decision 41.
- **FE7 — animation clip authoring.** Grid slicing and clip model in `arcane_editor/clip_authoring`, editor "Animation Clip" panel, mtime hot-reload for texture, clip and tileset caches. Decisions 42–43.
- **FE8 — analog input and rebinding.** Deadzones, button thresholds, four-pad routing, runtime rebinding with conflict detection, Input Map editor panel. Decisions 44–45.
- **FE9 — tileset terrain.** Optional per-tile solid/terrain metadata; 4-bit cardinal-neighbour auto-tiling brush with neighbour updates and single undo stroke. Decision 46 (8-bit blob not implemented).
- **FE10 — UTF-8 HUD text.** `stb_truetype` with the embedded Go Regular subset (BSD-3); glyph atlas, UTF-8 decode, measurement, fallback glyph. Decision 47.
- **FE11 — platformer validation slice.** `samples/platformer_slice` project and `samples/platformer_slice_game` module, headless sim and export tests, gap list in `docs/slice_platformer_gaps.md`. Decisions 48–49.
- **FE12 — partial.** `header_check` target compiles every public engine header standalone; int→float conversions made explicit (MSVC C4244 review proxy, gcc `-Wconversion`). Decision 50. The mingw exe run is **not** done (no wine on the box).

## Test results

Run with `scripts/build-and-test.sh` under `xvfb-run -a` for CTest (headless GL tests use Mesa llvmpipe).

| Config | Result |
|---|---|
| gcc Debug (required) | PASS — 36/36 |
| clang + ASan/UBSan | PASS — 36/36 |
| mingw-w64 cross-compile | PASS — build only, **exes not run** |

The last commit (`53759b9`) was built and tested with all three configs before it was committed.

## Delegated decisions

Logged in `docs/decisions.md` rows 31–50 (run 2 items): y-flip placement, HUD model and persistence, focus/click model, paint-tool undo granularity, platformer parameters, particle determinism, clip authoring model, hot-reload approach, analog and rebinding rules, 4-bit terrain (not 8-bit blob), UTF-8 font choice, slice layout, export test reuse, header-check and MSVC review scope. Row 38 records the branch/PR layout itself.

## Known issues and gaps

- Physics has no impulse API; the sample jump sets velocity directly.
- Platformer: no one-way platforms. Slice gap list in `docs/slice_platformer_gaps.md`.
- ParticleEmitter: no per-particle random frames, rotation, sub-emitters or multi-point curves; no dedicated editor panel (generic Inspector only).
- Clip editor: reopening an existing clip into the grid editor is not implemented; player does not hot-reload.
- HUD: no GL test for button drawing; no HiDPI pointer scaling; images are not serialized to HUD JSON.
- Text: no kerning, no non-Latin scripts; `font_scale` 1 is now an 8-unit em.
- Terrain: 4-bit cardinal mask only; no 8-bit blob autotiling.
- Editor ImGui widgets (tile paint mouse handling, Input Map, clip panel, tileset/terrain UI) are compile-checked only and have not been exercised interactively.
- Windows: `path.string()` uses the ANSI codepage for non-ASCII paths; not fixed.

## Next steps

- FE12 remainder: run the mingw-built player and test exes under wine when available; full pimpl/dllexport audit of every class added since master.
- FE13–FE15 not started (`jobs.conf`).
- Manual visual check of Play mode, the HUD and the particle preview.

## UNTESTED on native Windows/MSVC

Everything in this PR. The mingw build is cross-compiled only and was not run. Specifically:

- FE2 physics y-flip and sample jump.
- FE3 HUD input, focus and the editor HUD panel.
- FE4 tile paint tool (editor mouse handling compile-checked only).
- FE5 platformer controller and grounded detection.
- FE6 particle system, including the `test_particles_gl` offscreen run.
- FE7 clip authoring, the Animation Clip panel and mtime hot-reload.
- FE8 analog input, GLFW gamepad polling, rebinding, Input Map panel.
- FE9 terrain brush and tileset editor UI.
- FE10 UTF-8 font loading and atlas (embedded font data).
- FE11 platformer slice project and export.
- FE12 header-check target and the int→float casts.

## Stacking

This PR targets `engine-nightly-2026-10-26` (PR #3). Once PR #3 merges to master, retarget this PR to master.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
