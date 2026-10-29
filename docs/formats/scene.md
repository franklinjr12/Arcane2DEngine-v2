# `*.scene.json` — format `arcane.scene`, version 2

Schema: `schemas/arcane.scene.schema.json`. Rules shared with all files: `canonical-json.md`.

```json
{
  "format": "arcane.scene",
  "next_id": 3,
  "nodes": [
    {
      "components": [
        { "id": 2, "properties": { "primary": true, "zoom": 1.0 }, "type": "Camera" }
      ],
      "id": 1,
      "name": "MainCamera",
      "parent_id": 0,
      "position": [400.0, 300.0],
      "rotation_radians": 0.0,
      "scale": [1.0, 1.0]
    }
  ],
  "version": 2
}
```
(The writer prints the vectors one element per line; they are inline here for brevity.)

- `nodes` is a flat list in depth-first pre-order (parents before children, siblings in order);
  `parent_id` `0` means root. Sibling order is meaningful.
- Node and component IDs are nonzero, unique across the whole scene, and persist through edits and
  migration. `next_id` is the next unused ID and is at least `max(id) + 1`; IDs are never reused.
- `properties` holds exactly the properties the component type's registered descriptors declare
  (builtin or from the project's game module); the writer emits all of them, the reader accepts a
  subset (missing ones keep their default) and rejects unknown ones.
- Property value encodings: `bool` → boolean; `int` → integer; `float` → number; `vec2` → `[x, y]`;
  `color` → `[r, g, b, a]`; `string` → string; `asset_id` / `entity_id` → unsigned 64-bit integer.
- Optional on read (defaults): `parent_id` 0, `position` `[0, 0]`, `rotation_radians` 0, `scale`
  `[1, 1]`, `components` empty, `properties` empty, `next_id` derived.

## Tilemap layers

The builtin `Tilemap` component stores `layers` structurally: an array of layers (back to front),
each an array of `height` rows, each an array of `width` tile ids (`0` = empty, ids are 1-based, see
`tileset.md`). `solid_tiles` stays a comma-separated string of ids.

```json
"layers": [
  [ [1, 0, 2],
    [0, 3, 0] ],
  [ [0, 0, 0],
    [0, 0, 4] ]
],
"width": 3, "height": 2
```
(The writer prints one id per line.) The reader checks the shape against `width`/`height` whatever
the key order is: a wrong row count, row length, negative or non-integer id is an error. The older
text form (`"1,0,2,0,3,0;0,0,0,0,0,4"`, layers split by `;`, ids by `,`, missing trailing ids read as
0) is still read, strictly during migration: a non-numeric id or more ids than `width*height` fails
the migration instead of dropping cells. Writing always pads short layers to full rows.
