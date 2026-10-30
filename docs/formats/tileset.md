# `*.json` tileset — format `arcane.tileset`, version 2

Schema: `schemas/arcane.tileset.schema.json`. Rules shared with all files: `canonical-json.md`.
The file name is free (`tiles.json`, `*.tileset.json`); migration finds tilesets by content.

```json
{
  "columns": 4,
  "format": "arcane.tileset",
  "terrains": [{ "terrain": 1, "tiles": [1, 2, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2] }],
  "texture": 6606372916662563687,
  "tile_count": 8,
  "tile_height": 16,
  "tile_width": 16,
  "tiles": [{ "solid": true, "terrain": 1 }, { "solid": false, "terrain": 1 }],
  "version": 2
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `texture` | unsigned integer | asset id of the sheet image; required |
| `tile_width`, `tile_height`, `columns`, `tile_count` | integer ≥ 1 | the sheet is a regular grid; required |
| `tiles` | array | entry *n* describes tile id *n+1*; may be shorter than `tile_count` (missing = not solid, terrain 0) and not longer |
| `tiles[].solid` | boolean | default false |
| `tiles[].terrain` | integer ≥ 0 | terrain this tile belongs to (0 = none) |
| `terrains` | array | FE9 auto-tile rules, in order |
| `terrains[].terrain` | integer ≥ 1 | |
| `terrains[].tiles` | 16 integers | tile id (0..`tile_count`) for each neighbour mask 0..15 |

Author tilesets with `arcane tileset create/set_tile/set_rule/remove_rule/validate` (`docs/commands.md`).

Tile ids are 1-based, `0` is the empty cell. Unknown fields are errors; v1 differs only in the
header (`"format_version": 1`) and ignores unknown fields on plain loads.
