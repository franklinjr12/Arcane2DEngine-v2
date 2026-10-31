# Editor authoring command inventory

**AF8C update (current):** no authored data action is direct any more. Open clip is `clip.info` (with the panel's sheet and grid)
and Open tileset is `tileset.info`; Save clip is `clip.create` (overwrite) and Save tileset is one `tileset.replace`, both run
through `EditSession`, so each Save is **one undoable step** (undo restores the previous file bytes or removes a new file via
`clip.restore`/`tileset.restore`) and sets `*modified*`. The panels keep a draft model on purpose: edits to the draft fields
before Save (grid, frames, tile flags, terrain rules, New tileset) are unsaved view state like a text box, not authored data; the
authored change is the Save. Project Settings now also edits the scene transition (fade out/in, colour), save (game id, data
version) and debug overlay (visible, font scale) through `transition.set`, `save.config_set` and `debug.set`. Those three edit the
project data in memory like `project.set` (AF8C changed them from immediate `project.json` writes, which would have conflicted with
File > Save), so they undo/redo, coalesce slider drags and are written by File > Save (CLI: persisted per command). Covered by
`test_edit_session` (`test_gui_flow_settings_match_cli_model`, `test_tileset_save_is_one_undoable_command`,
`test_clip_open_and_save_through_commands`) and the extended source lint. The tables below are history; where they say
"No: immediate file write" for tileset/clip Save, "Still direct" or "no editor widget" for transition/save/debug, AF8C supersedes them.

**AF5B update:** the core scene paths below (hierarchy create/delete/reparent,
node name/transform, Add Component, Inspector properties incl. module types,
asset assignment to Sprite/ParticleEmitter/AnimatedSprite/AudioSource/Tilemap) now run
as registry commands through `arcane_tooling::EditSession` with undo/redo,
drag coalescing and save-point dirty tracking; `test_edit_session` lints
`editor_main.cpp` for direct scene writes. **AF6B update:** tilemap layer add, viewport brush, erase, rectangle fill and terrain brush now run as
`tilemap.*` commands through `EditSession` (one drag = one undo step via coalescing; Undo/Redo and Ctrl+Z use the
command history). The tileset panel still edits a draft in memory, but its Save now writes the file through
`tileset.create` (overwrite), `tileset.set_tile` and `tileset.set_rule` (not undoable per field). **AF7B update:** the HUD panel (add button/text/bar/image, remove, every field incl. image texture asset, UV and tint),
the Input Map panel (actions, player, bindings and every binding field) and clip **Save clip** now run as `hud.*`, `input.*`
and `clip.create` (overwrite) commands. HUD and input edits are in-memory project data run through `EditSession`
(undo/redo, drag coalescing, `*modified*`), written by File > Save; clip Save writes the file at once like the tileset Save.
The panels read `session.project()` only; `test_edit_session` lints `editor_main.cpp` for `mutable_project()`, `hud.add(`,
`.set_action(`, `.remove_action(`, `save_clip_file(` and `elements.erase(`. What remains direct: the tileset panel's draft editing
before Save, the clip panel's draft editing and **Open clip** (a read through `load_clip_file`; the draft is transient), and
project-level properties (AF8).

**AF8B update:** Project Settings (name, main scene, module) now runs through `project.set` via `EditSession`. Audio, physics,
particle and module Inspector properties and asset assignment run through `prop.set`; `test_edit_session` checks GUI/CLI scene
parity and undo/redo for AudioSource, RigidBody2D, Collider2D and ParticleEmitter properties, and that a Spinner (module) edit
persists across reopen and undoes. View-only state (selection, Play/Stop, preview toggles, the F3 debug overlay toggle, panel
drafts before Save) is not authored data and stays out of the history (tested for selection and Play/Stop). Still direct:
tileset/clip draft editing before Save and Open clip. Transition, save and debug settings have CLI commands (`transition.set`,
`save.config_set`, `debug.set`; immediate writes) but no editor widgets.

Historical list (before AF5B; **historical, superseded by the AF11B audit below**): these paths mutated authored data directly. Locations refer to `editor/app/editor_main.cpp`.

- Core scene (AF5): hierarchy creates, deletes and reparents nodes; Inspector
  changes node name and local transform, adds components and calls registered
  property setters. Asset selection assigns Sprite, ParticleEmitter,
  AnimatedSprite and AudioSource assets directly. Module-defined Inspector
  properties use the same direct setter path.
- Tile authoring (AF6): the tile panel adds layers, and viewport brush, erase,
  rectangle and terrain operations mutate Tilemap through TilePaintTool. Its
  separate stroke undo/redo is not a generic command history. The tileset panel
  edits a draft and writes the tileset file directly.
- Clip, HUD and input (AF7, before AF7B): the clip panel saved files through
  `save_clip_file`; HUD widgets edit project HUD elements; Input Map widgets
  add, change and remove actions and bindings.
- Remaining data actions (AF8): audit asset assignment and registered audio,
  physics, particle and project properties for parity, including module types.

Widget review for AF8: the Inspector uses registered descriptors for all
component types, including AudioSource (`clip`, `volume`, `loop`,
`play_on_start`), RigidBody2D, Collider2D, ParticleEmitter and module types;
all of their edits currently call setters directly. Project HUD and input
widgets mutate `ProjectFile` directly. There are no project name, main-scene
or module setting widgets in this editor. No separate physics material or
layer authoring exists. File > Save persists the current project and scene;
clip and tileset Save persist their respective drafts directly. Hierarchy
selection, asset browsing, paint mode/layer/tile selection, clip and tileset
draft values before Save, animation and particle preview, and Play/Stop are
transient. Paint undo/redo changes authored tiles; it is not view-only.
This review identifies missing coverage; no CLI or enforcement exists yet.

Selection, viewport and panel choices, paint brush selection, preview toggles,
play mode and unsaved panel drafts are transient editor state. A draft becomes
an authored change when saved; it must then use the shared command path.
This inventory is a starting point, not proof of exhaustive command coverage.

## AF11B audit (2026-10-30)

Reviewed `editor/app/editor_main.cpp` widget by widget. "Via EditSession" means the widget calls `edit(...)`/`run_edit(...)`
on `arcane_tooling::EditSession` (undo/redo, coalescing, `*modified*`). Immediate-write commands run through the registry but are
not in the scene history.

| GUI action | Command(s) | Via EditSession? | Covered by |
| ---------- | ---------- | ---------------- | ---------- |
| Hierarchy create/delete/reparent, node name/transform | `node.add/remove/rename/reparent`, `prop.set` (transform) | Yes | `test_edit_session` (GUI/CLI model parity, undo/redo, coalescing, save point), `test_cli` |
| Add Component, Inspector properties (incl. audio, physics, particle, module types), asset assignment | `component.add`, `prop.set` | Yes | `test_edit_session` (incl. `test_audio_physics_particle_props_match_cli`, `test_module_property_undo_after_reopen`) |
| Tilemap add layer, brush, erase, rect fill, terrain brush; paint Undo/Redo | `tilemap.add_layer/paint/erase/fill_rect/terrain_paint` | Yes (one drag = one step) | `test_tilemap_commands`, `test_edit_session` (undo/coalescing) |
| HUD add button/text/bar/image, remove, every field | `hud.add/remove/set` | Yes (in memory; File > Save writes) | `test_hud_input_commands`, `test_edit_session` (`test_gui_hud_input_matches_cli_model`, `test_hud_drag_coalesces_to_one_undo_step`) |
| Input Map actions, player, bindings and fields | `input.add_action/remove_action/set_player/add_binding/remove_binding/set_binding` | Yes (in memory; File > Save writes) | `test_hud_input_commands`, `test_edit_session` |
| Project Settings: name, main scene, module | `project.set` | Yes | `test_edit_session` (`test_gui_project_settings_match_cli_model`, name typing coalescing) |
| File > Save | `ProjectSession::save` (same transactional write as `ProjectContext::save`) | n/a | `test_edit_session` (save point), `test_cli` |
| Edit > Undo/Redo, Ctrl+Z/Y | command history | Yes | `test_edit_session` |
| Save tileset | `tileset.create` (overwrite), `tileset.set_tile`, `tileset.set_rule` | No: immediate file write, not undoable | `test_tilemap_commands` (command side only) |
| Save clip | `clip.create` (overwrite) | No: immediate file write, not undoable | `test_clip_commands` (command side only) |
| Selection, panels, brush choice, preview, Play/Stop, F3 overlay | none (view state) | n/a | `test_edit_session` (`test_view_state_is_not_authored`: selection, Play/Stop) |
| Editor source has no known direct writes | n/a | n/a | `test_edit_session` (`test_editor_source_has_no_direct_scene_writes`) |

CLI-only commands with no editor widget: `transition.set`, `save.config_set`, `debug.set`, `prefab.*` (no save-as-prefab,
instantiate, override, revert or break-link action in the GUI), `atlas.*`, `asset.*`, `replay.*`; `render` does not exist.

### Still direct / not command-backed

- Tileset panel: draft editing (texture, tile size, columns, tile count, tile flags, terrain rules, Add terrain rule) and **New tileset**
  (resets the in-memory draft; the file is created only by Save tileset). Only the Save step is registry-backed, and no test drives the
  panel's Save against the CLI result.
- Clip panel: draft editing (grid, frames, timing, name) and **Open clip** (a read through `load_clip_file`). Save clip is `clip.create`.
- Tileset/clip Save are immediate writes outside the EditSession history; they are not undoable and do not set `*modified*`.
- No automated parity test exists for the tileset and clip panels' GUI path, only for their commands.
- The direct-write lint in `test_edit_session` is a substring list (`->set_name(`, `create_node(`, `mutable_project()`, `hud.add(`,
  `save_clip_file(`, `.main_scene =` and similar). It finds only known patterns; it cannot discover a new kind of widget write.
