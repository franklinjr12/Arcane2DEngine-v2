# Authoring commands (`arcane_tooling`)

Every command is a name plus JSON arguments, run against an open
`ProjectContext` through `CommandRegistry::execute`. The library has no GUI
dependency. The `arcane` CLI (AF4B, `docs/cli-reference.md`) calls this registry; the
editor (AF5) is meant to but does not yet. The CLI also has file-level
commands that need no open scene (`project.*`, `scene.new`, `scene.list`,
`validate`, `fmt`, `migrate`, `schema`, `docs`); they are listed only in the
generated CLI reference.

## Project context

`ProjectContext::open(dir, module_override, scene_override)` registers builtin component
types, loads the project's native module (or `module_override`) and registers
its types, then reads the main scene, in that order. The scene is read
strictly: unknown component types or properties, or a missing module, fail the
open and nothing is written. The module stays loaded until the context is
destroyed; the scene and registry are destroyed first. Do not keep node,
component or descriptor pointers beyond the context.

`save()` writes the scene file and, when project-level data changed (`project_dirty()`, set by `hud.*` and `input.*`),
`project.json`. Both are serialized before anything is written, both files are written in one transactional
step (project.json first): all-or-nothing for ordinary failures, with the earlier file restored if the second
commit fails, but not crash-atomic (a kill between the two renames can leave a mix); a failure is `IO_ERROR` and keeps both dirty flags. It fails with `CONFLICT`, writing
nothing, if a file it would write changed on disk since it was read or last saved. Project-level commands edit
`session().mutable_project()` in memory only, are ordinary mutating commands (`persist` and `dry_run` work as
for scene commands, `dry_run` leaves the project clean) and never write on their own.

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

Structural edits (`node.remove`, `reparent`, `component.remove`, `scene.restore`) return a `scene.restore` inverse
holding the previous canonical scene, so node/component pointers from before such a call are invalid afterwards.
IDs are never reused within a session because the counter is part of the scene text, but undoing an add/remove
restores the counter too.

Node: `id:42` or `path:/Root/Player` (each name must be unique at its level).
Component: an integer component id, or a type name attached exactly once.

## Commands

| Command | Arguments | Result |
| --- | --- | --- |
| `prop.list` | `node`, `component` | `{component_id, type, properties:[{name,type,value}]}` |
| `prop.get` | `node`, `component`, `name` | `{name, type, value}` |
| `prop.set` (mutating) | `node`, `component`, `name`, `value` | `{changed, previous, value}` |
| `scene.tree` | `node?`, `depth?` | `{scene, nodes:[{id,name,components,children}]}` |
| `scene.query` | none | `{scene, nodes, roots, components, component_types, next_id}` |
| `scene.restore` (mutating, internal) | `text` | replaces the scene with canonical scene v2 text; the inverse of structural edits |
| `node.info` | `node` | `{id,name,path,selector,parent,transform,components,children,prefab?}` |
| `node.find` | `name?`, `name_contains?`, `component?`, `under?` | `{count, nodes:[{id,name,path,selector}]}` |
| `node.add` (mutating) | `name`, `parent?`, `position?`, `rotation_radians?`, `scale?` | `{id,name,path,selector}`; inverse `node.remove` |
| `node.remove` (mutating) | `node` | `{removed, nodes}`; refused for prefab-instance nodes and while an outside `entity_id` property references the subtree (`CONFLICT`) |
| `node.rename` (mutating) | `node`, `name` | `{changed, previous, name}`; names must not contain `/` |
| `node.reparent` (mutating) | `node`, `parent?` | moves the subtree to the end of the new parent (root if omitted); refuses cycles |
| `node.duplicate` (mutating) | `node`, `name?` (default `<name>_copy`), `parent?` | deep copy with new ids and remapped internal `entity_id` references |
| `node.set_transform` (mutating) | `node`, `position?`, `rotation_radians?`, `scale?` | `{changed, previous, transform}` |
| `component.types` | none | registered types with property names |
| `component.list` | `node` | `{node, components:[{id,type}]}` |
| `component.add` (mutating) | `node`, `type` | `{component_id,type,node}`; default property values |
| `component.remove` (mutating) | `node`, `component` | `{removed}`; calls the component's `on_destroy` |
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

Tilemaps and tilesets (`docs/formats/tileset.md`, `tooling/src/tilemap_commands.cpp`): cell commands take a `node` with a
`Tilemap` component and cell coordinates (`x`,`y`, 0-based, y down; `layer` defaults to 0). `tilemap.paint`, `erase`,
`fill_rect`, `terrain_paint` and `import_csv` all reduce to a list of changed cells whose inverse is one `tilemap.set_cells`
with the previous ids (the redo is the exact `set_cells`, so `CommandHistory` merges consecutive calls with the same
coalesce key into one stroke: the editor's drag = one undo step). Cells and tile ids are validated first (`VALUE_INVALID`
for out-of-grid cells, missing layers and ids above the tileset's `tile_count` when the map's tileset resolves); nothing
changes on error. `terrain_paint` is the editor's terrain brush (rule lookup by N/E/S/W mask, neighbours of that terrain
re-picked; `erase=true` clears and re-picks neighbours). CSV is one row per grid row, comma-separated non-negative
integers, equal row lengths, no trailing comma; `import_csv` takes `csv` text or a project-relative `file` at offset
`x`,`y`, `dump_csv` returns the text and with `file` writes it at once (so it refuses `--dry-run`).
`tileset.*` take a project-relative `tileset` path under `assets/` (the asset id derives from it) and write the file at once
(no `persist`/`dry_run`): `create` (texture as `assets/*.png` path or asset id), `set_tile` (solid/terrain),
`set_rule` (16 tile ids by neighbour mask N=1 E=2 S=4 W=8; replaces an existing rule), `remove_rule`, `info` and
`validate` (rule tiles inside `tile_count`, rule tiles marked with their terrain, duplicate/empty rules; `ok:false` lists
problems and the file is never changed). Their inverses are the matching tileset commands. Add layers with
`tilemap.add_layer`; resize maps with `prop.set` on `width`/`height`.

## HUD commands

`hud.*` edit the HUD layout stored in `project.json` (`docs/formats/hud.md`), addressing elements by 0-based
`index` into the draw order. Registered by `register_hud_commands`; see `tooling/src/hud_commands.cpp`.

| Command | Arguments | Result |
| --- | --- | --- |
| `hud.list` | none | `{count, elements:[{index,type,id,...fields}]}`; `type` is `text`, `image`, `bar` or `button` |
| `hud.add` (mutating) | `type`, `index?` (default append), `element?` (field overrides) | `{index, id}`; inverse `hud.remove` |
| `hud.remove` (mutating) | `index` | `{index,id,type}`; inverse `hud.add` with every field and the index |
| `hud.set` (mutating) | `index`, `field`, `value` | `{index,field,previous,value,changed}`; inverse `hud.set` with the previous value |
| `hud.move` (mutating) | `from`, `to` | `{from,to,changed}`; inverse `hud.move` with the indices swapped |

Defaults match the editor's `+` buttons (button id `button` label `Button`; text `Text`, white; default bar; default
image). A new element without an `id` gets an unused one (`button`, then `<type>_<n>`); ids are unique and non-empty.
Fields: all types `id`, `anchor` (`TopLeft`, `Top`, `TopRight`, `Left`, `Center`, `Right`, `BottomLeft`, `Bottom`,
`BottomRight`), `offset_x`, `offset_y`; text `text`, `font_scale`, `color`; image `texture_asset` (image asset id,
0 = none; a non-zero id must name an image under `assets/`), `uv` `[u0,v0,u1,v1]`, `width`, `height`, `tint`;
bar `width`, `height`, `fraction` (0..1), `fill`, `background`; button `label`, `width`, `height`, `font_scale`,
`enabled`, `normal`, `hover`, `pressed`, `disabled`, `text_color`. Colors are `[r,g,b,a]`; sizes are >= 0 and
`font_scale` > 0. Runtime-only state (`HudImage::texture`, button `state`/`focused`) is never exposed. An unknown
field is `ARGS_INVALID` with the valid names in the hint, a bad value or index `VALUE_INVALID`; a rejected call changes
nothing. Setting a field to its current value, or moving an element onto itself, is a no-op without an inverse.

## Input commands

`input.*` edit the input action map stored in `project.json` (`input_actions`, `docs/formats/input.md`). Actions are
addressed by name, bindings by 0-based `index`. Registered by `register_input_commands`; see
`tooling/src/input_commands.cpp`.

| Command | Arguments | Result |
| --- | --- | --- |
| `input.list` | none | `{count, actions:[{name,player,bindings:[{device,code,pad,axis_sign,deadzone,threshold}]}]}` |
| `input.add_action` (mutating) | `name`, `player?` (-1..3, default -1) | `{name,player}`; inverse `input.remove_action` |
| `input.remove_action` (mutating) | `action` | `{name}`; inverse `input.restore_action` with the full action and its position |
| `input.restore_action` (mutating) | `action` (object from `input.list`), `index?` (default append) | `{name,index}`; inverse `input.remove_action` |
| `input.set_player` (mutating) | `action`, `player` | `{action,previous,player,changed}`; inverse `input.set_player` |
| `input.add_binding` (mutating) | `action`, `device?`, `code?`, `pad?`, `axis_sign?`, `deadzone?`, `threshold?`, `index?` | `{index}`; inverse `input.remove_binding` |
| `input.remove_binding` (mutating) | `action`, `index` | `{index}`; inverse `input.add_binding` with every field and the index |
| `input.set_binding` (mutating) | `action`, `index`, `field`, `value` | `{action,index,field,previous,value,changed}`; inverse `input.set_binding` with the previous value |

`device` is `keyboard`, `mouse_button`, `gamepad_button` or `gamepad_axis` (the persisted names; default `keyboard`).
Ranges: `pad` -1..3 (-1 = the action's player), `axis_sign` -1..1, `deadzone` 0..0.99, `threshold` 0..1, `code` >= 0;
the axis fields are only persisted for `gamepad_axis`. Action names are unique and non-empty. Action order is the
persisted order; `input.restore_action` rebuilds the map to insert at `index`, so undo restores equal content and order.
An unknown action or out-of-range index is `VALUE_INVALID` (hint: list with `input.list`); a rejected call changes
nothing, and setting a field to its current value is a no-op without an inverse.

## Clip commands

`clip.*` slice a sprite sheet and author animation clip files (`docs/formats/clip.md`). Like `atlas.*` and `tileset.*` they
read and write asset files only, at once (no `persist`/`dry_run`, the CLI refuses `--dry-run`). Paths (`clip`, `sheet`) are
project-relative `/` paths; clips normally live under `assets/`. The sheet's pixel size is read from the PNG file without
a GL context, so only PNG sheets are supported (`VALUE_INVALID` otherwise, `IO_ERROR` if missing). Registered by
`register_clip_commands`; see `tooling/src/clip_commands.cpp`.

Grid arguments (the editor's Animation Clip panel fields): `columns`, `rows` (>= 1), `frame_width`, `frame_height`
(0 = sheet size / count), `margin`, `spacing` (>= 0). Cells are numbered row-major from the top-left.

| Command | Arguments | Result |
| --- | --- | --- |
| `clip.slice` | `sheet`, grid | `{sheet_width, sheet_height, cells:[{index, uv:[u0,v0,u1,v1]}]}`; an empty cell is `VALUE_INVALID` |
| `clip.info` | `clip`, `sheet?` + grid | `{name, loop, frames:[{uv,seconds}]}`; with a sheet also `{cells, durations, fps}` |
| `clip.create` | `clip`, `sheet`, grid, `cells` or `from`+`to`, `fps?` (10), `durations?`, `loop?`, `name?`, `overwrite?` | `{clip, frames, cells}`; inverse `clip.restore` |
| `clip.add_frames` | `clip`, `sheet`, grid, `from`, `to`, `index?` (append) | `{clip, frames, added, index, changed}` |
| `clip.remove_frame` | `clip`, `sheet`, grid, `index` | `{clip, frames, index, changed}` |
| `clip.move_frame` | `clip`, `sheet`, grid, `from`, `to` | `{clip, frames, from, to, changed}` |
| `clip.set_frame` | `clip`, `sheet`, grid, `index`, `cell?`, `seconds?` | `{clip, frames, index, changed}` |
| `clip.set` | `clip`, `name?`, `loop?` | `{clip, name, loop, changed}`; no grid needed, frames are kept exactly |
| `clip.restore` | `clip`, `content` or `absent=true` | `{clip, restored}`; only used as the inverse of the others |

`clip.create` builds the clip with `ClipDraft` + `build_clip` (a `durations` entry of 0 means `1/fps`) and refuses an
existing file (`CONFLICT`) unless `overwrite=true`. The edit commands reopen the clip with `draft_from_clip` on the grid
you pass: clip files do not store their grid, so every frame must be exactly one grid cell, otherwise the call fails with
`VALUE_INVALID` naming the frame and the file is untouched. Durations are kept per frame; frames added by `add_frames` use
`1/fps` of the first frame (change them with `set_frame --seconds`). Setting a value to what it already is writes nothing
and has no inverse. Every write returns `clip.restore` carrying the previous file text (or `absent` for a new file), so
undo restores the exact bytes.

## Project commands

`project.get` / `project.set` read and edit the project-level properties in `project.json` (`name`, `main_scene`,
`module`). Like `hud.*` they edit the project in memory and mark it dirty; File > Save (or `--persist`) writes
`project.json`. Nothing is reloaded: `main_scene` and `module` take effect the next time the project is opened.
Registered by `register_project_commands`; see `tooling/src/project_commands.cpp`.

| Command | Arguments | Result |
| --- | --- | --- |
| `project.get` | none | `{name, main_scene, module}` |
| `project.set` (mutating) | `name?`, `main_scene?`, `module?` (at least one) | `{changed, previous, project}`; inverse `project.set` with the previous values |

`name` must be non-empty. `main_scene` must be a project-relative `/` path without `..` that ends in `.json`.
`module` may be empty (no native module) or a project-relative path. Anything else is `VALUE_INVALID` (file
`project.json`, path `/name`, `/main_scene` or `/module`) and nothing is changed, even if another argument was valid.
Setting a value to what it already is is a no-op without an inverse.

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

