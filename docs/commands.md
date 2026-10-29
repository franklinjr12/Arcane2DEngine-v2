# Authoring commands (`arcane_tooling`)

Every command is a name plus JSON arguments, run against an open
`ProjectContext` through `CommandRegistry::execute`. The library has no GUI
dependency. The `arcane` CLI (AF4) and the editor (AF5) are meant to call
the same registry; neither does yet, so today the commands are reachable from
C++ and `tests/test_tooling_commands.cpp` only.

## Project context

`ProjectContext::open(dir, module_override)` registers builtin component
types, loads the project's native module (or `module_override`) and registers
its types, then reads the main scene, in that order. The scene is read
strictly: unknown component types or properties, or a missing module, fail the
open and nothing is written. The module stays loaded until the context is
destroyed; the scene and registry are destroyed first. Do not keep node,
component or descriptor pointers beyond the context.

`save()` writes the scene file only (serialized first, replaced atomically,
old file kept on failure) and fails with `CONFLICT` if the file changed on disk
since it was read or last saved.

## Execution

`execute(context, {command, args}, {persist, dry_run})`:

- arguments are checked against the command's schema (`ARGS_INVALID`);
- a mutating command returns `inverse`, the invocation that restores the
  previous state (absent when nothing changed);
- `persist` saves after the command and restores memory if saving fails;
  `dry_run` runs and then restores memory, writing nothing;
- a failed command leaves memory and disk unchanged.

`CommandHistory` records forward/inverse pairs for undo/redo (the GUI model;
the CLI persists immediately and keeps no history).

## Selectors

Node: `id:42` or `path:/Root/Player` (each name must be unique at its level).
Component: an integer component id, or a type name attached exactly once.

## Commands

| Command | Arguments | Result |
| --- | --- | --- |
| `prop.list` | `node`, `component` | `{component_id, type, properties:[{name,type,value}]}` |
| `prop.get` | `node`, `component`, `name` | `{name, type, value}` |
| `prop.set` (mutating) | `node`, `component`, `name`, `value` | `{changed, previous, value}` |
| `atlas.pack` | `folder`, `atlas`, `image?`, `padding?` (default 1), `max_size?` (default 2048) | `{atlas, image, width, height, padding, regions:[{name,x,y,w,h,uv}]}`; writes the PNG and `arcane.atlas` file at once (ignores `persist`/`dry_run`) |
| `atlas.regions` | `atlas`, `name?` | same shape as `atlas.pack`, read only |
| `atlas.make_clip` | `atlas`, `regions:[name]`, `clip`, `name?`, `frame_seconds?` (0.1), `loop?`, `overwrite?` | `{clip, frames}`; writes the clip file, `CONFLICT` if it exists without `overwrite` |
| `transition.get` | none | `{fade_out_seconds, fade_in_seconds, color}` from `project.json` |
| `transition.set` | `fade_out_seconds?`, `fade_in_seconds?` (0..60), `color?` `[r,g,b,a]` | `{changed, previous, transition}`; writes `project.json` at once (ignores `persist`/`dry_run`); inverse is `transition.set` with the previous values |
| `debug.get` | none | `{overlay_visible, overlay_font_scale}` from `project.json` |
| `debug.set` | `overlay_visible?`, `overlay_font_scale?` (>0, <=8) | `{changed, previous, debug}`; writes `project.json` at once (ignores `persist`/`dry_run`); inverse is `debug.set` with the previous values |
| `replay.save` | `file`, `frames`, `fixed_hz?`, `seed?`, `events?` | `{file, frames, events}`; writes a validated `arcane.replay` v1 file (`docs/formats/replay.md`) at once (ignores `persist`/`dry_run`) |
| `replay.run` | `recording`, `expected?`, `trace_out?` | `{frames, final_hash, hashes, matches_expected}`; replays on a copy of the scene (module `on_update` is not called), `trace_out` writes the trace, `expected` fails with `REPLAY_DIVERGED` (path `/hashes/<frame>`) at the first differing frame |
| `bench.run` | `sprites?`, `emitters?`, `particles_per_emitter?` (0..256), `bodies?`, `frames?` (1..100000) | `arcane.bench` v1 document `{format, version, config, results}`; headless, touches no project file; see `docs/perf.md` |
| `save.config_get` | none | `{game_id, data_version, directory}` |
| `save.config_set` | `game_id?`, `data_version?` (>= 1) | `{changed, previous, save}`; writes `project.json` at once; inverse restores the previous values |
| `save.list` | `dir?` | `{directory, slots:[{slot, data_version, valid}]}` |
| `save.get` | `slot`, `key?`, `dir?` | `{data_version, values}` or `{key, value, data_version}` |
| `save.set` | `slot`, `key`, `value`, `create?`, `dir?` | `{slot, key, value, previous}`; atomic write; `create` makes a missing slot at the project's `data_version` |
| `save.remove` | `slot`, `key`, `dir?` | `{slot, removed}` |
| `save.migrate` | `slot`, `to?` (default `save.data_version`), `rename?`, `remove?`, `defaults?`, `dir?` | `{slot, from_version, data_version, values}`; never downgrades; a failure writes nothing |
| `audio.backend` | `probe_device?` (default false) | `{backend:"null"\|"miniaudio", probed, diagnostic}` |
| `prefab.save` | `node`, `asset`, `overwrite?` | `{asset, nodes}`; writes the file at once (ignores `persist`/`dry_run`) |
| `prefab.instantiate` (mutating) | `asset`, `parent?`, `name?`, `position?`, `overrides?` | `{instance_id, nodes:[{local_id,id}]}` |
| `prefab.remove` (mutating) | `node` (any node of the instance) | `{removed}`; undo re-instantiates with new IDs and the same overrides |
| `prefab.override` (mutating) | `node`, `component?`, `property`, `value` | `{instance_id, local_id, property, value, overridden, changed}` |
| `prefab.revert` (mutating) | `node`, `component?`, `property` | `{reverted, ...}`; no-op if not overridden |
| `prefab.break_link` (mutating) | `node` | `{instance_id, nodes}`; inverse is `prefab.relink` |
| `prefab.relink` (mutating) | `asset`, `instance_id`, `nodes:[{id,local_id}]`, `overrides?` | `{instance_id, nodes}` |
| `prefab.info` | `node` | `{linked, asset, instance_id, local_id, overrides}` |

Atlases (`docs/formats/atlas.md`): all paths are project-relative `/` paths; `image` defaults to the `atlas` path with `.png`
in place of `.json`. Packing is deterministic. Register with `register_atlas_commands`.

Prefabs (`docs/formats/prefab.md`): `asset` is a project-relative path ending in `.prefab.json`.
`node` selectors for override/revert/break_link/remove/info may name any node of an instance.
`overrides` entries are `{local_id, component?, property, value}` with prefab-local IDs; `name` and
`position` on `prefab.instantiate` are shortcuts for root overrides. The root's `name`/`position`
differing from the prefab are ordinary overrides. A stale redo of `prefab.instantiate` creates new
IDs, so a history entry recorded before an undo/redo cycle may not find the old instance.

Transitions and saves (`docs/formats/project.md`, `docs/formats/save.md`): without `dir` the save commands use
`save_directory(save.game_id)` and fail with `VALUE_INVALID` if no `game_id` is set. They touch only `project.json`
and save files, never the open scene. Register with `register_flow_commands`.

`audio.backend` opens no audio device unless `probe_device` is true; without it
the answer is always `null`. With it, a device is opened briefly and, if that
fails, `backend` is `null` and `diagnostic` holds the reason (the same fallback
visible editor/player sessions use). Its failure code `AUDIO_UNAVAILABLE`
(neither backend could start) has CLI exit status 1. Audio settings are the
`AudioSource` properties (`clip`, `volume`, `loop`, `play_on_start`), edited
with `prop.set`.

Values use the scene encodings (`docs/formats/scene.md`): bool, 32-bit int,
finite binary32 float, `[x,y]`, `[r,g,b,a]`, string, unsigned id. `entity_id`
values must be 0 or an existing node.

## Errors

`{"ok":false,"error":{"code","message","file","path","hint"}}`, absent fields
null. Exit status for the CLI:

| Code | Exit |
| --- | --- |
| `UNKNOWN_COMMAND`, `ARGS_INVALID`, `USAGE` | 2 |
| `PROJECT_NOT_FOUND`, `IO_ERROR` | 3 |
| `SCHEMA_INVALID`, `VALUE_INVALID`, `PROPERTY_UNKNOWN`, `NODE_NOT_FOUND`, `COMPONENT_NOT_FOUND`, `COMPONENT_TYPE_UNKNOWN`, `REPLAY_DIVERGED` | 4 |
| `SELECTOR_AMBIGUOUS`, `CONFLICT` | 5 |
| `MODULE_LOAD_FAILED`, `MODULE_FAILURE` | 6 |
| `UNSUPPORTED_VERSION` | 7 |

## Tween component

There are no `tween.*` commands. The builtin `Tween` component's properties (`target`, `from`, `to`,
`use_current_from`, `duration`, `delay`, `easing`, `loops`, `yoyo`, `autoplay`) are edited with
`prop.list`/`prop.get`/`prop.set` like any component. `target` is one of `position.x`, `position.y`,
`rotation`, `scale.x`, `scale.y`, `tint.r|g|b|a` (needs a `Sprite`); `easing` is one of `linear`,
`quad_in|out|in_out`, `cubic_in|out|in_out`, `sine_in|out|in_out`, `back_out`, `bounce_out`. Unknown
names are accepted by `prop.set` but make the tween do nothing. `loops` 0 repeats forever.

