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
- Not verified visually in this environment (hidden Xvfb runs only); see RESULT/PR notes.

## Benchmark

`arcane_bench` (`build/bin/arcane_bench`) and the registry command
`bench.run` run the same code (`run_benchmark`, `arcane/debug.hpp`). It needs no window, GL or audio: a scene
of N sprite nodes, E particle emitters bursting P particles each (kept alive for the whole run) and B dynamic
bodies falling onto a static floor is updated and physics-stepped for M fixed 60 Hz frames. **Rendering is not
measured** (that needs a GL context); it measures scene update, particle update and Box2D.

```sh
arcane_bench [--sprites N] [--emitters N] [--particles-per-emitter N] [--bodies N] [--frames N]
```

Defaults: 1000 sprites, 10 emitters, 100 particles each, 200 bodies, 120 frames. Ranges: counts 0..1000000
(emitters, bodies 0..100000; particles per emitter 0..256), frames 1..100000. Output on stdout is an
`arcane.bench` v1 document with sorted keys:

```json
{"config": {"bodies": 200, "emitters": 10, "frames": 120, "particles_per_emitter": 100, "sprites": 1000},
 "format": "arcane.bench", "version": 1,
 "results": {"avg_frame_ms": 0.0, "live_particles": 1000, "max_frame_ms": 0.0,
             "physics_bodies": 201, "setup_ms": 0.0, "total_ms": 0.0}}
```

Keys and counts (`live_particles`, `physics_bodies` = bodies + 1 floor) are stable; the `*_ms` values vary per
machine. Bad arguments exit with status 2 and a message on stderr. CTest runs a tiny variant
(`bench_cli_smoke`, plus two bad-argument cases and `test_debug`); the large runs below are manual.

## Baseline

Release build (gcc 14, `-DCMAKE_BUILD_TYPE=Release`), 8-vCPU Xeon VM, 300 frames, one run each (noisy):

| Scene | avg frame ms | max frame ms |
| --- | --- | --- |
| 1000 sprites | 0.009 | 0.022 |
| 10000 sprites | 0.351 | 0.833 |
| 100 emitters x 100 particles | 0.019 | 0.384 |
| 500 bodies | 0.095 | 0.347 |
| 2000 bodies | 0.353 | 0.545 |

Treat these as an order-of-magnitude reference for spotting regressions on the same machine, not as a target.

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
