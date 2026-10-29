# `project.json` — format `arcane.project`, version 2

Schema: `schemas/arcane.project.schema.json`. Rules shared with all files: `canonical-json.md`.

```json
{
  "format": "arcane.project",
  "hud": { "elements": [], "format_version": 1 },
  "input_actions": { "actions": [], "format_version": 1 },
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
| `input_actions` | object | nested document, own `format_version: 1`; becomes `format/version` in a later job |
| `hud` | object | same |

Unknown fields are errors. v1 differs only in the header (`"format_version": 1`).
