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

## FE6 — Particle / VFX system
- `ParticleEmitter` (rate, burst, lifetime/speed/angle ranges, gravity, color+alpha and size lerp over life, texture or atlas frame, local/world space, max particles, deterministic seed), fixed-step CPU sim, drawn through the sprite batch in `render_scene`. Inspector/JSON via properties; editor "Preview particles" toggle (Edit mode, does not dirty the scene); Assets panel click assigns the texture; sample scene has a "Sparks" emitter. See decision 41.
- Tests: `test_particles` (rate/burst counts, expiry, cap, seed determinism, motion/gravity/space, atlas UV, JSON round trip) and `test_particles_gl` (offscreen FBO pixels; skips without GL).
- Not done: texture-per-particle random frames, rotation/angular velocity, sub-emitters, color/size curves beyond 2 points, editor UI beyond the generic Inspector.
- **UNTESTED on native Windows/MSVC** (`ParticleEmitter` has out-of-line ctor/dtor; no pimpl).

## FE7 — Animation clip editor + hot-reload
- Editor "Animation Clip" panel: pick a sheet, set grid (cols/rows/frame size/margin/spacing), add/reorder/remove frames, per-frame duration or fps, loop, live preview, save to `assets/*.json`. Logic lives in ImGui-free `clip_authoring.{hpp,cpp}`. Decisions 42-43.
- Hot-reload: `TextureCache`/`AnimationClipCache`/`TilesetCache::refresh()` poll file mtimes; editor polls every 0.5 s (also rescans assets). Player does not poll.
- Tests: `test_clip_authoring` (grid slicing, draft edit/build, save->load round trip, cache invalidation by mtime incl. broken/deleted file). Texture invalidation shares the code path but is not GL-tested.
- Not done: reopening an existing clip into the grid editor (grid metadata is not stored), the ImGui panel itself is only compile-checked, not driven.
- **UNTESTED on native Windows/MSVC** (no pimpl; `std::filesystem` mtime only).
