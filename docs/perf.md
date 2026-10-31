# Debug overlay and performance benchmark (FE23)

## Debug overlay

Six lines: FPS, frame time (ms, averaged over ~0.5 s), draw calls, sprites, physics bodies, particles.

- **F3** toggles it in `arcane_player` (drawn with the HUD font in the top-left corner) and in `arcane_editor`
  (a small "Debug overlay (F3)" window; bodies are counted only in Play mode).
- Whether it starts visible and its font scale are the `debug` section of `project.json`
  (`docs/formats/project.md`), read and written with `debug.get` / `debug.set` (`docs/commands.md`). The GUI
  only toggles the shown state in memory; it never writes the project.
- Draw calls and sprites come from `SpriteRenderer::stats()` (reset by `begin_frame`). The overlay's own
  glyphs are drawn after the numbers are read, so they are not counted. A "sprite" is one accepted
  `SpriteDraw`, including tiles, particles and HUD quads.
- The overlay does not show the new `culled` counter; it is available in `RenderStats` only (`SpriteRenderer::stats()`).
- Not verified visually in this environment (hidden Xvfb runs only); see RESULT/PR notes.

## Benchmark

`arcane_bench` (`build/bin/arcane_bench`) and the registry command
`bench.run` run the same code (`run_benchmark`, `arcane/debug.hpp`). It needs no window, GL or audio: a scene
of N sprite nodes, E particle emitters bursting P particles each (kept alive for the whole run) and B dynamic
bodies falling onto a static floor is updated and physics-stepped for M fixed 60 Hz frames. **Rendering is not
measured by `bench.run` or by `arcane_bench` without `--render 1`** (that needs a GL context); that mode measures
scene update, particle update and Box2D. See the GL render benchmark below.

```sh
arcane_bench [--sprites N] [--emitters N] [--particles-per-emitter N] [--bodies N] [--frames N] [--render 0|1]
```

Defaults (the PF1 scene): 5000 sprites, 20 emitters, 100 particles each (2000 particles), 500 bodies, 120 frames. Ranges: counts 0..1000000
(emitters, bodies 0..100000; particles per emitter 0..256), frames 1..100000. Output on stdout is an
`arcane.bench` v1 document with sorted keys:

```json
{"config": {"bodies": 500, "emitters": 20, "frames": 120, "particles_per_emitter": 100, "sprites": 5000},
 "format": "arcane.bench", "version": 1,
 "results": {"avg_frame_ms": 0.0, "live_particles": 2000, "max_frame_ms": 0.0,
             "physics_bodies": 501, "setup_ms": 0.0, "total_ms": 0.0}}
```

Keys and counts (`live_particles`, `physics_bodies` = bodies + 1 floor) are stable; the `*_ms` values vary per
machine. Bad arguments exit with status 2 and a message on stderr. CTest runs a tiny variant
(`bench_cli_smoke`, plus two bad-argument cases and `test_debug`); the large runs below are manual.

## Baseline

These older tables were measured with the old defaults (1000 sprites, 100 emitters), before PF1.
Release build (gcc 14, `-DCMAKE_BUILD_TYPE=Release`), 8-vCPU Xeon VM, 300 frames, one run each (noisy):

| Scene | avg frame ms | max frame ms |
| --- | --- | --- |
| 1000 sprites | 0.009 | 0.022 |
| 10000 sprites | 0.351 | 0.833 |
| 100 emitters x 100 particles | 0.019 | 0.384 |
| 500 bodies | 0.095 | 0.347 |
| 2000 bodies | 0.353 | 0.545 |

## GL render benchmark (`--render 1`)

`arcane_bench --render 1` (handled by the executable, not by `bench.run`) also runs `run_render_benchmark`
(`arcane_editor/render_bench.hpp`): the same scene plus a primary Camera over a 1280x720 view (the 1600 px wide
sprite grid is partly culled), drawn each frame with `render_scene` into an offscreen framebuffer followed by
`glFinish`. It adds a `render` object (`render_avg_frame_ms`, `render_max_frame_ms`, `update_avg_ms`, `draw_calls`,
`sprites`, `culled`, `live_particles`, `physics_bodies`, `width`, `height`) and needs a display or `xvfb-run -a`;
without one it exits 8. `test_render_bench` (label `gl`) checks the counts on the PF1 scene: sprites + culled = 7000
quads, culled > 0 and at most 3 draw calls (2 observed: sprites and particles use different textures). Timings under
llvmpipe in Debug are far slower than on a GPU and only useful for comparing runs on the same machine.

Treat these as an order-of-magnitude reference for spotting regressions on the same machine, not as a target.

## Rendering and entity performance (PF1)

**Batching** (`SpriteRenderer`, `engine/src/renderer.cpp`): quads are collected per contiguous run of the same
texture and drawn with one call; a texture change or `end_frame()` flushes. The index buffer is static (built
once), the vertex buffer is orphaned (`glBufferData(nullptr)`) before each upload so the driver need not
synchronize, and a run longer than `kSpriteMaxBatchQuads` (16384) is split into several calls. Unrotated sprites
skip `sin`/`cos`.

**Culling** (on by default, `SpriteRenderer::set_culling`): `sprite_in_view()` is conservative. Unrotated sprites
use their exact box, rotated ones a circle through every corner, so a visible sprite is never dropped. Culled
sprites count in `RenderStats::culled`, not in `stats.sprites`. Tilemaps are culled separately in `scene_render`
(`editor/editor_core/src/scene_render.cpp`) by computing the visible cell range, so only those cells are submitted.
`arcane_bench` has no flag to disable culling, so no with/without numbers were measured.

**Regression tests**: `test_render_bench` (label `gl`: PF1 scene counts, sprites + culled = 7000, culled > 0, at
most 3 draw calls), `test_render_cull` (visibility rules and stats), the PF1 case in `test_debug` (counts only, no
timing), and `bench_cli_render_smoke` (`--render 1`, skips on exit 8). None asserts wall-clock time.

**Measured numbers.** Noisy: one machine (2 vCPU, g++ 15.2.0), Release, 300 frames, 5000 sprites, 2000 particles,
500 bodies, three runs per mode. The machine was shared with other
builds. These come from Mesa llvmpipe (software GL) under Xvfb, not real GPU hardware, so the render times say
nothing about GPU speed. Windows was not measured.

| Mode | avg frame ms (runs) | max frame ms (runs) |
| --- | --- | --- |
| default (update + physics, no render) | 3.31, 3.79, 4.05 | 8.97, 13.03, 9.52 |
| `--render 1`, update part (`update_avg_ms`) | 1.87, 1.76, 1.65 | n/a |
| `--render 1`, `render_avg_frame_ms` | 554, 545, 457 | 842, 730, 730 |

In the render runs: 4850 sprites drawn, 2150 culled, 2 draw calls (1280x720 view).

## Arena slice (GS1)

`samples/topdown_slice` replayed headless for 3600 fixed frames (60 s of game time, `tests/arena_60s.replay.json`: WASD
moves, Space fires, a pause in the middle). Rendering is not measured: `--headless` skips the window and GL.

Machine: 2 vCPU (`nproc` = 2) VM, g++ 15.2.0, Release (`cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release`).

```sh
env -u DISPLAY build-rel/bin/arcane_player --headless --project samples/topdown_slice \
  --module build-rel/bin/topdown_slice_game.so --replay samples/topdown_slice/tests/arena_60s.replay.json
```

| Run | wall time, 3600 frames |
| --- | --- |
| 1 | 0.306 s |
| 2 | 0.289 s |
| 3 | 0.288 s |

About 0.08 ms per frame on average, including process start and loading the module (so this is an upper bound for the
simulation alone). Peak live enemies and bullets were not recorded. `arcane_bench` with its defaults (1000 sprites,
100 particle emitters): 0.167 ms average and 0.278 ms maximum frame, 120 frames.

The recorded trace is only valid for the build that produced it: the Release run above reports a divergence from the
Debug-recorded `arena_60s.trace.json` at frame 60 (determinism is per build and platform, see `docs/formats/replay.md`).

## Platformer slice (GS2)

`samples/platformer_slice` replayed headless for 620 fixed frames (`samples/platformer_slice/tests/level2_run.replay.json`,
about 10 s of game time). The headless runner has no SceneManager, so this run stays in level 1 (the goal does not
switch levels; see `docs/slice_platformer_gaps.md` gap 13). Rendering is not measured. Same machine and build as the
arena numbers above (2 vCPU VM, g++ 15.2.0, Release).

```sh
env -u DISPLAY build-rel/bin/arcane_player --headless --project samples/platformer_slice \
  --module build-rel/bin/platformer_slice_game.so --replay samples/platformer_slice/tests/level2_run.replay.json
```

| Run | wall time, 620 frames (`/usr/bin/time`, 10 ms resolution) |
| --- | --- |
| 1 | 0.03 s |
| 2 | 0.02 s |
| 3 | 0.02 s |

Under 0.05 ms per frame including process start and module load (an upper bound for the simulation alone). Peak
enemies and bodies were not recorded.
