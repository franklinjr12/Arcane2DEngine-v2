# `*.json` animation clip — format `arcane.clip`, version 2

Schema: `schemas/arcane.clip.schema.json`. Rules shared with all files: `canonical-json.md`.
The file name is free (`hero_idle.json`, `*.clip.json`); migration finds clips by content.

```json
{
  "format": "arcane.clip",
  "frames": [
    { "duration_seconds": 0.5, "uv": [0.0, 0.0, 0.25, 1.0] },
    { "duration_seconds": 0.1, "uv": [0.25, 0.0, 0.5, 1.0] }
  ],
  "loop": true,
  "name": "idle",
  "version": 2
}
```

`frames` is required and its order is the playback order. `uv` is `[u0, v0, u1, v1]` into the
sprite's texture. `duration_seconds` is ≥ 0 (default 0.1 when absent on read, always written).
`name` defaults to empty and `loop` to true. Unknown fields are errors in v2 and during migration.
