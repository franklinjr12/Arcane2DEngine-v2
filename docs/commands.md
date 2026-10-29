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
| `audio.backend` | `probe_device?` (default false) | `{backend:"null"\|"miniaudio", probed, diagnostic}` |
| `prefab.save` | `node`, `asset`, `overwrite?` | `{asset, nodes}`; writes the file at once (ignores `persist`/`dry_run`) |
| `prefab.instantiate` (mutating) | `asset`, `parent?`, `name?`, `position?`, `overrides?` | `{instance_id, nodes:[{local_id,id}]}` |
| `prefab.remove` (mutating) | `node` (any node of the instance) | `{removed}`; undo re-instantiates with new IDs and the same overrides |
| `prefab.override` (mutating) | `node`, `component?`, `property`, `value` | `{instance_id, local_id, property, value, overridden, changed}` |
| `prefab.revert` (mutating) | `node`, `component?`, `property` | `{reverted, ...}`; no-op if not overridden |
| `prefab.break_link` (mutating) | `node` | `{instance_id, nodes}`; inverse is `prefab.relink` |
| `prefab.relink` (mutating) | `asset`, `instance_id`, `nodes:[{id,local_id}]`, `overrides?` | `{instance_id, nodes}` |
| `prefab.info` | `node` | `{linked, asset, instance_id, local_id, overrides}` |

Prefabs (`docs/formats/prefab.md`): `asset` is a project-relative path ending in `.prefab.json`.
`node` selectors for override/revert/break_link/remove/info may name any node of an instance.
`overrides` entries are `{local_id, component?, property, value}` with prefab-local IDs; `name` and
`position` on `prefab.instantiate` are shortcuts for root overrides. The root's `name`/`position`
differing from the prefab are ordinary overrides. A stale redo of `prefab.instantiate` creates new
IDs, so a history entry recorded before an undo/redo cycle may not find the old instance.

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
| `SCHEMA_INVALID`, `VALUE_INVALID`, `PROPERTY_UNKNOWN`, `NODE_NOT_FOUND`, `COMPONENT_NOT_FOUND`, `COMPONENT_TYPE_UNKNOWN` | 4 |
| `SELECTOR_AMBIGUOUS`, `CONFLICT` | 5 |
| `MODULE_LOAD_FAILED`, `MODULE_FAILURE` | 6 |
| `UNSUPPORTED_VERSION` | 7 |
