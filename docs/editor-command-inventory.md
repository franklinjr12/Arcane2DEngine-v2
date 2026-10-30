# Editor authoring command inventory

**AF5B update:** the core scene paths below (hierarchy create/delete/reparent,
node name/transform, Add Component, Inspector properties incl. module types,
asset assignment to Sprite/ParticleEmitter/AnimatedSprite/AudioSource/Tilemap) now run
as registry commands through `arcane_tooling::EditSession` with undo/redo,
drag coalescing and save-point dirty tracking; `test_edit_session` lints
`editor_main.cpp` for direct scene writes. **AF6B update:** tilemap layer add, viewport brush, erase, rectangle fill and terrain brush now run as
`tilemap.*` commands through `EditSession` (one drag = one undo step via coalescing; Undo/Redo and Ctrl+Z use the
command history). The tileset panel still edits a draft in memory, but its Save now writes the file through
`tileset.create` (overwrite), `tileset.set_tile` and `tileset.set_rule` (not undoable per field). The remaining direct paths are the tileset
panel, clip/HUD/input (AF7) and project-level properties (AF8).

Historical list (before AF5B): these paths mutated authored data directly. Locations refer to `editor/app/editor_main.cpp`.

- Core scene (AF5): hierarchy creates, deletes and reparents nodes; Inspector
  changes node name and local transform, adds components and calls registered
  property setters. Asset selection assigns Sprite, ParticleEmitter,
  AnimatedSprite and AudioSource assets directly. Module-defined Inspector
  properties use the same direct setter path.
- Tile authoring (AF6): the tile panel adds layers, and viewport brush, erase,
  rectangle and terrain operations mutate Tilemap through TilePaintTool. Its
  separate stroke undo/redo is not a generic command history. The tileset panel
  edits a draft and writes the tileset file directly.
- Clip, HUD and input (AF7): the clip panel saves files through
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

## AF11 review

A widget-by-widget review of `editor_main.cpp` also identifies **New tileset**
(which creates a file), tileset texture selection and terrain-rule edits followed
by **Save tileset**, clip grid/frame edits followed by **Save clip**, and tile
paint Undo/Redo as authored operations requiring registry-backed equivalents.
The input panel changes player assignment and binding device, code, pad, axis
direction, deadzone and threshold as well as adding/removing actions and bindings.
HUD creation, removal and field edits are authored; the panel currently offers
button, text and bar creation, but no image creation. Hierarchy drag/drop is a
reparent operation. The selected asset can change component asset properties.

Camera/panel/selection controls, preview toggles, Play/Stop and unsaved draft
fields are transient; saving a draft is an authored operation. This inventory is
an audit of the present editor, not an automated guarantee: registry, CLI,
coverage manifest, routing and direct-write lint remain absent. Completion needs
behavioral parity tests as well as a manifest; a manifest alone cannot discover
new direct widget writes.

- Prefabs (FE18): no editor action exists yet for save-as-prefab, instantiate, override, revert or break-link; they are `prefab.*` registry commands only. The GUI should call them (not a separate path) when AF5 routes the hierarchy and Inspector.
