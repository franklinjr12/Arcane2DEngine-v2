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
- A node may carry a `prefab` link (`prefab.md`); scenes without any remain valid.
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

## Camera2D

`Camera2D` is a registered component, so its properties use the standard v2 property encodings and appear in the Inspector. `primary` selects the first primary Camera2D in scene preorder for Play and player; absent one, the legacy `Camera` remains in use. Edit viewport behavior is unchanged. `target` is a scene node ID (0 disables following); `deadzone` is the world-space half-size around the camera and `smoothing` is an exponential rate per second (0 follows immediately). A missing target uses the camera node's world position. `zoom` defaults to 1; nonpositive values render as 1.

`bounds_enabled`, `bounds_min`, and `bounds_max` define an explicit world rectangle, including tilemap extents if entered by the author. The viewport is clamped inside it; if the viewport is larger than an axis of the rectangle, that axis is centered. `pixel_snap` rounds the final center to screen-pixel increments at the current zoom. `shake_amplitude`, `shake_duration`, and integer `shake_seed` configure a deterministic, linearly decaying shake starting at Play; C++ can restart it with `start_shake()`. Camera tracking and shake advance on fixed scene ticks; transient state is not serialized. Clamp precedes pixel snapping, so snapping can move the view by at most half a screen pixel beyond a bound.

Every Camera2D property can be read and set with the shared `prop.get` / `prop.set` commands (`docs/commands.md`), which validate the value encoding before writing; `tests/test_tooling_commands.cpp` round-trips all settings, including a seeded shake that replays identically after reopening the project. Gap: the `arcane` CLI (AF4) and node/component creation commands do not exist yet, so adding a Camera2D to a scene still happens in the editor or by editing canonical scene JSON, and the editor Inspector does not yet route through the commands.

## AudioSource

The registered `AudioSource` properties are `clip` (asset ID), `volume` (float),
`loop` (boolean), and `play_on_start` (boolean). They are stored in the scene
component properties and legacy scenes remain readable. At Play/start, the clip
ID is resolved against project audio assets; looping sources use the music bus
and other sources use the SFX bus. The shared CLI is not yet available (AF3/AF4
are partial); these properties can currently be authored in scene JSON or the
editor Inspector.

## One-way platforms

`Collider2D.one_way` (boolean, default false; absent in older scenes) marks a static box collider as one-way: bodies land on it from above and pass through from below. `PlatformerController2D.down_action` (string, default `move_down`) and `drop_duration` (float seconds, default 0.5) let a grounded character hold down and press jump to drop through for that interval; the drop timer is runtime-only and never saved. All three are ordinary registered properties, so `prop.get` / `prop.set` read and write them and `tests/test_tooling_commands.cpp` round-trips them through a saved scene; landing, passing from below and dropping are covered headlessly by `tests/test_platformer.cpp`. Not done: a per-tile one-way flag (tilemap colliders are merged solid boxes) and slopes. The `arcane` CLI does not exist yet, so there is no CLI spelling beyond the registry commands.
