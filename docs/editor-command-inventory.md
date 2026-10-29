# Editor authoring command inventory

AF5 is blocked on the unfinished AF3 registry and AF4 CLI. The following
existing paths still mutate authored data directly; none has command coverage
or command-routing lint yet. Locations refer to `editor/app/editor_main.cpp`.

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
