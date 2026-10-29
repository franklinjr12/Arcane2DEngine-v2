# HUD — format `arcane.hud`, version 2 (nested in `project.json` as `hud`)

Schema: `schemas/arcane.hud.schema.json`. Rules shared with all files: `canonical-json.md`.

```json
{
  "elements": [
    { "anchor": "top_left", "color": [1, 1, 1, 1], "font_scale": 3.0, "id": "text_1",
      "offset": [16, 16], "text": "SCORE 0", "type": "text" },
    { "anchor": "bottom_right", "asset": 6606372916662563687, "id": "image_1", "offset": [-8, -8],
      "size": [48, 48], "tint": [1, 1, 1, 1], "type": "image", "uv": [0, 0, 1, 1] }
  ],
  "format": "arcane.hud",
  "version": 2
}
```

`elements` is in draw order (later is on top) and that order is preserved. Every element has a
`type` (`text`, `image`, `progress_bar`, `button`) and a stable, unique, non-empty string `id`.
`anchor` is one of `top_left top top_right left center right bottom_left bottom bottom_right`;
`offset` and `size` are in layout pixels at 720p height.

| Type | Extra fields (defaults if absent on read; all are written) |
| --- | --- |
| `text` | `text`, `font_scale` 2, `color` |
| `image` | `asset` (texture asset id, 0 = none), `uv` `[0,0,1,1]`, `size` `[32,32]`, `tint` |
| `progress_bar` | `size` `[200,16]`, `fraction`, `fill`, `background` |
| `button` | `label`, `size` `[160,40]`, `font_scale`, `enabled`, `normal`, `hover`, `pressed`, `disabled`, `text_color`; default anchor `center` |

Ids: legacy HUDs had ids only on buttons. Migration (and plain legacy loads) keep explicit ids and
give every other element, in file order, the first free `<kind>_<n>` (`text_1`, `bar_1`,
`image_1`, `button_1`; `n` counts per kind from 1 and skips ids in use). Elements created in code
or by the editor with an empty or repeated id get the same treatment when saved (the first
occurrence keeps a repeated id). Strict reads (v2, migration) reject a duplicate explicit id and
unknown fields. Runtime state (button hover/pressed/focus, resolved texture pointer) is never saved;
`HudImage::texture_asset` is the persisted reference.
