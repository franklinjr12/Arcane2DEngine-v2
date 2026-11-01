# `*.prefab.json` — format `arcane.prefab`, version 1

Schema: `schemas/arcane.prefab.schema.json`. Rules shared with all files: `canonical-json.md`.
Scene-side data: the `prefab` object on scene nodes (`schemas/arcane.scene.schema.json`).

A prefab is a reusable node subtree. The file is a scene file with `format` `arcane.prefab`,
`version` 1, **exactly one root node** (`parent_id` 0) and no `prefab` objects (prefabs do not nest;
saving a subtree that contains an instance flattens it). Node and component IDs inside the file are
**local IDs**: they come from one counter, are unique within the file and never change once the
prefab is saved, so overrides can name them. Save with `prefab.save` (below) rather than by hand.

```json
{ "format": "arcane.prefab", "version": 1, "next_id": 6,
  "nodes": [ { "id": 1, "name": "Rig", "parent_id": 0, "components": [ ... ] },
             { "id": 5, "name": "Hand", "parent_id": 1, "components": [ ... ] } ] }
```

## Instances in scenes

Instancing copies the prefab into the scene with fresh scene IDs and stores, on **every** instance
node, a link:

```json
"prefab": { "asset": "prefabs/rig.prefab.json", "instance_id": 7, "local_id": 5 }
```

- `asset` is a project-relative path (`canonical-json.md`).
- `instance_id` is the scene ID of the instance root; the root's own link has `instance_id` equal to
  its node `id`. It is the stable handle of the instance.
- `local_id` is the node's ID in the prefab file.
- The instance root additionally carries `overrides`, an explicit list of differences from the
  prefab: `{ "local_id": 5, "component": "Sprite", "property": "width", "value": 99.5 }`. Without
  `component` the property is a node field: `name`, `position`, `rotation_radians` or `scale`.
  Values use the scene encodings. Tilemap `layers` cannot be overridden.
- The node itself always holds the full current values; `overrides` records which of them differ
  from the prefab, so the file is readable without the prefab and an override can be reverted.
  Setting a property back to the prefab's value drops its override.
- `EntityId` properties pointing at other nodes inside the instance are remapped to the new copies on
  instancing; ones pointing outside the prefab are cleared to 0.

Loading rejects a link whose `instance_id` is not an instance root with the same `asset`, overrides
on a non-root node, unknown override properties and unknown link keys. Scenes without any `prefab`
objects (all older scenes) load and save unchanged; no migration is needed.

Not done: prefab edits do not propagate to existing instances (re-instantiate, or edit the
instance), prefab nesting, and adding or removing nodes inside one instance while keeping the link.

## Commands (`docs/commands.md`)

`prefab.save`, `prefab.instantiate`, `prefab.remove`, `prefab.override`, `prefab.revert`,
`prefab.break_link`, `prefab.relink`, `prefab.info`. Runtime: `arcane::load_prefab_file`,
`instantiate_prefab` and `spawn_prefab` in `arcane/prefab.hpp` (load once, instantiate N times).
