# Input actions — format `arcane.input`, version 2 (nested in `project.json` as `input_actions`)

Schema: `schemas/arcane.input.schema.json`. Rules shared with all files: `canonical-json.md`.

```json
{
  "actions": [
    { "bindings": [{ "code": 65, "device": "keyboard" }, { "code": 0, "device": "gamepad_button", "pad": 1 }],
      "name": "move_left" },
    { "bindings": [{ "axis_sign": -1, "code": 0, "deadzone": 0.2, "device": "gamepad_axis", "threshold": 0.5 }],
      "name": "steer", "player": 0 }
  ],
  "format": "arcane.input",
  "version": 2
}
```

Action order is preserved (names are unique and non-empty); binding order within an action too.
`device` is `keyboard`, `mouse_button`, `gamepad_button` or `gamepad_axis`; `code` is the GLFW key /
button / axis code.

Defaults (what an older file without the field means, and what the writer omits): `pad` -1 (use the
action's `player`, else any pad) and `player` -1 (any). For `gamepad_axis` bindings the writer always
writes `axis_sign` (0 = either direction), `deadzone` (0.2) and `threshold` (0.5); on other devices
those three fields are not allowed. Ranges: `pad`, `player` -1..3; `axis_sign` -1..1; `deadzone`
0..0.99; `threshold` 0..1. Plain legacy loads clamp out-of-range values as before; v2 reads and
migration reject them.
