# engine-nightly-2026-10-27 (stacked on engine-nightly-2026-10-26 / PR #3)

## FE3 — HUD buttons and input routing
- `HudButton` (states, focus outline), `HudInput` router, `MaskedInputBackend`, HUD JSON in `project.json`, editor "HUD" panel, player/Play wiring. See decisions 33-37.
- Tests: `test_hud_input` (hit-testing at 720p/1440p, state transitions, click consumption, focus navigation, JSON round trip) and the project round trip in `test_scene_json`.
- Not covered by automated tests: GL drawing of buttons, the editor HUD panel, real mouse/gamepad hardware. HiDPI pointer scaling not handled.
- **UNTESTED on native Windows/MSVC** (new `HudInput`/`MaskedInputBackend` exports use only std members; no pimpl or unique_ptr of incomplete types).
