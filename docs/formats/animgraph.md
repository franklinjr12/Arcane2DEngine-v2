# Animation state machine — format `arcane.animgraph`, version 2

Schema: `schemas/arcane.animgraph.schema.json`. Example: `tests/golden/schema_examples/animgraph.example.json`.
Rules shared with all files: `canonical-json.md`. The file name is free (`hero.animgraph.json` by convention) and it
lives under `assets/`; its asset id is `asset_id_for_relative_path` of the path below `assets/`, like clips. There is no
legacy dialect; migration only re-checks and re-writes it canonically.

```json
{
  "format": "arcane.animgraph",
  "initial": "idle",
  "parameters": [{ "default": 0.0, "name": "moving", "type": "bool" }],
  "states": [
    { "clip": 4478366437428034728, "events": [], "name": "idle", "speed": 1.0 },
    { "clip": 6756191263469248021, "events": [{ "frame": 0, "name": "step" }], "name": "run", "speed": 1.0 }
  ],
  "transitions": [
    { "conditions": [{ "op": "is_true", "param": "moving" }], "from": "idle", "priority": 0, "to": "run" },
    { "conditions": [{ "op": "is_false", "param": "moving" }], "from": "run", "priority": 0, "to": "idle" }
  ],
  "version": 2
}
```

* `parameters`: `name` (unique), `type` `bool` | `float` | `trigger`, `default` (bool: 0 or 1; absent for triggers).
  A trigger is set by gameplay and stays set until a transition with a `fired` condition takes it.
* `states`: `name` (unique, not `*`), `clip` (clip asset id, 0 = none), `speed` (≥ 0, AnimatedSprite speed), `events`:
  `{frame, name}` delivered when playback enters clip frame `frame` (looping clips deliver again on every pass; the
  frame-0 event fires on entering the state). `initial` names the first state (required when there are states).
* `transitions`: `from` (state name or `*` for any state, which never re-enters the current state), `to`, `priority`
  (integer, higher wins), `conditions` (all must hold; empty = always). Among the transitions that hold, the highest
  priority wins and ties go to the earlier one. One transition is taken per tick.
* Condition `op`: `is_true`/`is_false` (bool), `lt le gt ge eq ne` with `value` (float), `fired` (trigger).

Runtime: the `Animator` component (property `graph` = the asset id) sits next to an `AnimatedSprite` and sets its clip,
speed and restart on every state change. Gameplay calls `set_bool/set_float/set_trigger` on it; frame events go to
`Component::on_animation_event` of every component on the node. Events are attached to states, not to clip files.
Strict reads reject unknown fields, duplicate names, unknown states or parameters and operators that do not fit the
parameter type.
