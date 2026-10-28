# engine-nightly-2026-10-27 (stacked on engine-nightly-2026-10-26 / PR #3)

## FE3 — HUD buttons and input routing
- `HudButton` (states, focus outline), `HudInput` router, `MaskedInputBackend`, HUD JSON in `project.json`, editor "HUD" panel, player/Play wiring. See decisions 33-37.
- Tests: `test_hud_input` (hit-testing at 720p/1440p, state transitions, click consumption, focus navigation, JSON round trip) and the project round trip in `test_scene_json`.
- Not covered by automated tests: GL drawing of buttons, the editor HUD panel, real mouse/gamepad hardware. HiDPI pointer scaling not handled.
- **UNTESTED on native Windows/MSVC** (new `HudInput`/`MaskedInputBackend` exports use only std members; no pimpl or unique_ptr of incomplete types).

## FE5 — Platformer character controls
- `PlatformerController2D` (run accel/decel, max speed, fall gravity, jump cut, coyote time, jump buffer; fixed-update, input action map), `RigidBody2D::grounded` filled from Box2D contacts, per-step `gravity_scale` sync. Sample scene now has a collidable tilemap ("Ground" + tileset asset) and the Player uses the controller. See decision 40.
- Tests: `test_platformer` (headless, fake input, fixed steps: grounded on tilemap, max speed/decel, jump-cut apex, coyote jump allowed in window and refused after, buffered jump on landing, JSON round trip).
- Not done: one-way platforms. Sample Play mode was only smoke-run for 120 frames (no visual check). The old sample `PlayerController` stays in `sample_game.so` but is no longer in the scene.
- **UNTESTED on native Windows/MSVC** (`PlatformerController2D` has out-of-line ctor/dtor; no pimpl).
