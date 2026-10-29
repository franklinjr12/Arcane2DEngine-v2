# `*.json` sprite atlas — format `arcane.atlas`, version 2

Schema: `schemas/arcane.atlas.schema.json`. Rules shared with all files: `canonical-json.md`.
The file name is free (`ui.atlas.json`); the packed image is a PNG (binary, reproducible from the
source folder). Migration ignores atlas files.

```json
{
  "format": "arcane.atlas",
  "height": 64,
  "image": "assets/art.atlas.png",
  "padding": 2,
  "regions": [
    { "h": 8, "name": "coin", "w": 8, "x": 2, "y": 2 },
    { "h": 24, "name": "hero/idle_0", "w": 16, "x": 12, "y": 2 }
  ],
  "version": 2,
  "width": 64
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `image` | project-relative path | the atlas PNG; required |
| `width`, `height` | integer ≥ 1 | image size in pixels; required |
| `padding` | integer ≥ 0 | empty pixels around the image edge and between regions (default 0 on read) |
| `regions[].name` | string | source PNG path relative to the packed folder, no extension, `/` separators; unique, sorted bytewise |
| `regions[].x`, `y`, `w`, `h` | integer | pixel rectangle, origin top-left; must lie inside the image |

UV rects are not stored; they are `[x/width, y/height, (x+w)/width, (y+h)/height]` as binary32
(`atlas_region_uv`), the same `[u0, v0, u1, v1]` a clip frame uses. Unknown fields are errors.

## Packing

`pack_atlas_layout` / `pack_atlas_folder` (`arcane/atlas.hpp`) use shelf packing. Inputs are ordered
by height (descending), width (descending), then name, so the result does not depend on directory
order. The width is the smallest power of two (≤ `max_size`) giving an atlas that is not taller than
wide; the height is the exact shelf total. Same inputs give the same bytes (PNG and JSON). Source
files are `*.png` (any case) found recursively; the output image itself is skipped when it sits in
the folder. Sources are decoded to RGBA; empty space is transparent black.

## Use from sprites, clips and export

- `atlas.make_clip` writes an `arcane.clip` whose frames are atlas regions (UVs copied from the
  atlas). The sprite using that clip keeps `Sprite.texture` pointing at the atlas image.
- `Sprite` itself has no region property yet; its `uv` stays a rect. Use `atlas.regions` to read
  a region's UV.
- `scripts/export-game.sh` copies the project's `assets/` folder wholesale, so atlas JSON and PNG
  placed there are exported; nothing else is needed.
