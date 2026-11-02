# `*.replay.json` input recording and `*.trace.json` state trace — formats `arcane.replay` / `arcane.replay_trace`, version 1

Schemas: `schemas/arcane.replay.schema.json`, `schemas/arcane.replay_trace.schema.json`. Rules shared with all
files: `canonical-json.md`. API: `arcane/replay.hpp`, `arcane/screenshot.hpp`. Both files are plain text, so an
agent can write, read and diff them without a GUI. Recordings live anywhere under the project (the sample uses
`replays/`).

**Determinism promise: same build, same platform only.** Floating point, Box2D and the C++ library may give other
results on another compiler, OS or CPU. A trace from one machine is not expected to match another; regenerate it
there. Nothing here is promised across engine versions either.

## Recording

```json
{
  "events": [
    {"code": 68, "down": true, "frame": 5, "kind": "key"},
    {"code": 68, "down": false, "frame": 80, "kind": "key"}
  ],
  "fixed_hz": 60,
  "format": "arcane.replay",
  "frames": 150,
  "seed": 0,
  "version": 1
}
```

(Shown compact; the files themselves are canonical: sorted keys, one array element per line.)

| Field | Type | Notes |
| --- | --- | --- |
| `format` | `"arcane.replay"` | required |
| `version` | `1` | required; other values are refused |
| `frames` | integer 1..1000000 | required; number of fixed frames to run |
| `fixed_hz` | integer 1..1000 | default 60; every frame advances `1/fixed_hz` seconds, never wall-clock time |
| `seed` | integer >= 0 | default 0 = keep authored values. Otherwise every `Int` property named `seed` on every component (particle emitters, camera shake) is set to it before frame 0 |
| `events` | array | input CHANGES, ordered by `frame` (each `0 <= frame < frames`); a value stays in effect until changed again. Unknown fields are errors |

Event kinds (all have `frame` and `kind`):

| `kind` | other fields |
| --- | --- |
| `key`, `mouse_button` | `code` (GLFW code, as in `input.md`), `down` |
| `pad_button` | `pad` (0..3), `code` (GLFW gamepad button), `down` |
| `pad_axis` | `pad` (0..3), `code` (0..5, `GamepadAxes`), `value` in [-1, 1] |
| `mouse_move` | `x`, `y` (window pixels) |

Frame `n` applies its events first, then updates the input actions, then runs one `Scene::update` and one physics
step, then hashes. HUD hit-testing is not part of a replay (only the project's input actions see the input).

## Trace

```json
{"format": "arcane.replay_trace", "hashes": ["a1b2c3d4e5f60718", "..."], "version": 1}
```

One 16-digit lowercase hex FNV-1a 64 hash per frame. The hash covers, in tree order: every node's id, name, local
transform (float bits, `-0` equals `+0`), pending-destroy flag, and each component's id, type name and every
registered property value; then the live particle count. State that is not a registered property (for example a
private member of a game component, or the module's own variables) is NOT hashed. Expose it as a property to have
it checked. Game-module `on_update` runs in the player (`--replay`) but not in `replay.run` (see below), so a trace
is only comparable with one produced the same way.

## Running a replay

- Command `replay.run` (`docs/commands.md`): replays on a COPY of the open scene, so the project and its files are
  untouched; `trace_out` writes a trace, `expected` compares and fails with `REPLAY_DIVERGED` naming the first
  divergent frame (a shorter or longer trace diverges at the shorter length). Exit status 4.
- Command `replay.save`: writes a recording from JSON arguments (validated like a file).
- Player: `arcane_player --hidden --project <dir> --module <so> --replay <file> [--trace-out <f>]
  [--expect-trace <f>] [--screenshot <png>]`. One fixed step per rendered frame, keyboard ignored, exits after the
  last frame. Exit 0 ok, 3 diverged (the log names the first divergent frame), 1 error, 77 no GL display.
- C++: `ReplayRecorder` (call `capture` once per fixed frame with a live `InputBackend`, then `finish()`),
  `run_replay`, `hash_scene_state`, `first_divergent_frame`.

## Rendering evidence

`capture_framebuffer(w, h)` reads the bound framebuffer (after `SpriteRenderer::end_frame`) into top-to-bottom
RGBA8; `write_png` / `read_png` use the pinned `stb_image_write` / `stb_image`; `compare_images(a, b, tolerance)`
reports the largest channel delta and how many pixels differ by more than `tolerance`. `test_render_golden`
renders a fixed 160x120 scene and compares it with `tests/golden/render/basic.png`: each channel may differ by at
most 2 and at most 0.5% of the pixels may differ by more. The golden comes from Mesa llvmpipe under Xvfb (the
`scripts/build-and-test.sh` environment); a different GL driver can differ more, so regenerate with
`ARCANE_UPDATE_GOLDEN=1 test_render_golden` and review the diff. Screenshots are an evidence aid, not part of the
trace hash.
