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

Selection, viewport and panel choices, paint brush selection, preview toggles,
play mode and unsaved panel drafts are transient editor state. A draft becomes
an authored change when saved; it must then use the shared command path.
This inventory is a starting point, not proof of exhaustive command coverage.
