# `project.json` — format `arcane.project`, version 2

Schema: `schemas/arcane.project.schema.json`. Rules shared with all files: `canonical-json.md`.

```json
{
  "format": "arcane.project",
  "hud": { "elements": [], "format": "arcane.hud", "version": 2 },
  "input_actions": { "actions": [], "format": "arcane.input", "version": 2 },
  "main_scene": "scenes/main.scene.json",
  "module": "sample_game.so",
  "name": "sample_project",
  "version": 2
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `format` | `"arcane.project"` | required |
| `version` | `2` | required |
| `name` | string | required |
| `main_scene` | project-relative path | required; the scene opened at startup |
| `module` | project-relative path or `""` | native game module (`.so`/`.dll`); not binary engine data, it is compiled user code |
| `input_actions` | object | nested `arcane.input` v2 document, see `input.md` |
| `hud` | object | nested `arcane.hud` v2 document, see `hud.md` |
| `transition` | object | optional scene-switch fade, below; omitted when all defaults |
| `save` | object | optional save-game settings, below; omitted when unset |
| `debug` | object | optional debug overlay settings, below; omitted when default |
| `collision_layers` | array of strings | optional names of collision layers (`a-z0-9_`, 1-32 chars, unique, at most 31); the index is the layer number used by `Collider2D.layer` / `mask`; omitted when empty. Edit with `project.set --collision_layers` |

### `transition`

```json
"transition": { "color": [0.0, 0.0, 0.0, 1.0], "fade_in_seconds": 0.3, "fade_out_seconds": 0.3 }
```

`fade_out_seconds` / `fade_in_seconds` (number, 0..60, default 0.3) and `color` (RGBA, default opaque black)
configure `SceneManager` (`arcane/scene_flow.hpp`). Edit with `transition.get` / `transition.set`.

### `debug`

```json
"debug": { "overlay_font_scale": 2.0, "overlay_visible": true }
```

`overlay_visible` (boolean, default false) says whether the F3 debug overlay is shown at startup;
`overlay_font_scale` (number, > 0 and <= 8, default 1.5) is its HUD font scale. Edit with `debug.get` /
`debug.set`. Unknown fields are errors; the section is omitted when both values are the defaults. See `perf.md`.

### `save`

```json
"save": { "data_version": 2, "game_id": "my_game" }
```

`game_id` (`A-Za-z0-9._-`, 1-64 characters, default `""` = none) names the per-user save folder;
`data_version` (integer >= 1, default 1) is the game's current save layout version (see `save.md`).
Edit with `save.config_get` / `save.config_set`. Both sections reject unknown fields. Sections that equal the
defaults are not written, so existing projects keep their bytes.

Nested documents of the first AF1 build (`format_version: 1` inside a v2 project) are still read and upgraded on migration. Unknown fields are errors. v1 differs only in the header (`"format_version": 1`).
