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
- A root node may carry `"persistent": true` (boolean, written only when true): `SceneManager` carries it
  over scene switches with its live state. On a node with a `parent_id` the strict reader rejects it and the
  compatible (legacy) reader ignores it. If a carried root and an authored persistent root of the incoming
  scene share a name, the incoming one is destroyed (the live object wins); non-persistent incoming nodes
  with that name stay. Duplicating a node and instancing a prefab do not copy the flag; play/stop scene
  copies round-trip it with the rest of the scene.
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

`look_ahead` (Vec2, world units) leads the followed target in the direction it last moved on each axis (standing still keeps the previous lead). `bounds_tilemap` is a node ID whose `Tilemap` world rectangle (`tile size * cells * node scale`, from the node position) replaces `bounds_min`/`bounds_max`; 0 or a node without a Tilemap falls back to the explicit rectangle. Both are plain registered properties (`prop.set`, Inspector).

## Sprite

`Sprite` properties: `texture` (asset id), `atlas` (asset id of an `arcane.atlas` file, 0 = none), `region` (atlas region name, e.g. `hero/idle_0`), `width`, `height`, `tint`, `flip_x`, `flip_y`. With `atlas` and `region` set and resolving, the sprite draws that region of the atlas image; otherwise it draws `texture` (see `atlas.md` for precedence). Older files without `atlas`/`region` load with the defaults (0, empty).

## ParallaxLayer

`ParallaxLayer` sits on a node with a `Sprite` and draws it shifted by `camera * (1 - factor)` per axis (`factor` Vec2, default `[0.5, 0.5]`): 1 moves with the world, 0 stays fixed on screen. `repeat_x` / `repeat_y` tile the sprite (its scaled width/height is the period) across the whole visible rectangle, so it works with any `Camera2D.zoom` and viewport size. Layers draw in scene order like any Sprite: put background nodes before the level. Modules can read the view size with `GameContext::viewport_size()`.

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

`Collider2D.layer` (integer 0..30, default 0) and `Collider2D.mask` (integer bit mask of layers it reacts to, default 2147483647 = all) filter collisions, trigger events and spatial queries; layer names live in `project.json` `collision_layers`. A sensor (`is_sensor`) never pushes; it raises `Component::on_trigger_enter/exit` on itself and the visitor, and solid shapes raise `on_collision_enter/exit` (dispatched after each step, exits before enters, ordered by node id). `PhysicsWorld::raycast/overlap_aabb/overlap_point` (also via `GameContext::physics()`) take a layer mask and ignore sensors. Covered by `tests/test_physics_events.cpp`.

`Collider2D.one_way` (boolean, default false; absent in older scenes) marks a static box collider as one-way: bodies land on it from above and pass through from below. `PlatformerController2D.down_action` (string, default `move_down`) and `drop_duration` (float seconds, default 0.5) let a grounded character hold down and press jump to drop through for that interval; the drop timer is runtime-only and never saved. All three are ordinary registered properties, so `prop.get` / `prop.set` read and write them and `tests/test_tooling_commands.cpp` round-trips them through a saved scene; landing, passing from below and dropping are covered headlessly by `tests/test_platformer.cpp`. Not done: a per-tile one-way flag (tilemap colliders are merged solid boxes) and slopes. The `arcane` CLI does not exist yet, so there is no CLI spelling beyond the registry commands.

## Top-down controller

`TopDownController2D` (registered with the physics components) moves a node's sibling `RigidBody2D` in four directions; give the body `gravity_scale` 0 and `fixed_rotation`. Properties: `max_speed` (float px/s, default 160), `acceleration` (1200) and `deceleration` (1600) in px/s^2, `normalize_diagonal` (bool, default true), `left_action`/`right_action`/`up_action`/`down_action` (strings, defaults `left`/`right`/`up`/`down`) and `flip_sprite` (bool, default false; sets the sibling `Sprite.flip_x` from the horizontal facing). Facing and moving state are runtime-only and never saved. Covered by `tests/test_topdown_controller.cpp`.

## Steering agent

`SteeringAgent2D` (registered with the physics components) moves a node's sibling `RigidBody2D` (give it `gravity_scale` 0 and `fixed_rotation`) toward the node named by `target` (string). Properties: `behavior` (int: 0 seek, 1 arrive, 2 flee; default 0), `max_speed` (float px/s, default 90), `max_acceleration` (600 px/s^2), `slow_radius` (48, arrive only), `stop_distance` (4; seek/arrive stop this close to the target), `use_pathfinding` (bool, default true; seek/arrive follow a grid A* path, flee ignores paths), `repath_interval` (seconds, default 0.5), `tilemap` (string node name of the map to plan on; empty = first node with a `Tilemap`) and `allow_diagonal` (bool, default true). Cells with a solid tile in any layer of the tilemap are blocked. The path, waypoint index, repath timer and any `set_target_position` override are runtime-only and never saved. Covered by `tests/test_navigation.cpp`.

## Spawner2D and the runtime active flag

`Spawner2D` (registered with the physics components) is a pooled prefab spawner. Properties: `prefab` (string, project-relative `*.prefab.json`), `prewarm` (int, default 0; inactive instances created when the spawner starts), `max_alive` (int, default 16, 0 = unlimited), `grow` (bool, default true; instantiate when no parked instance is left), `interval` (float seconds, default 0 = only manual `spawn()` calls), `auto_start` (bool, default true), `offset` (Vec2 added to the spawner's world position for interval spawns) and `spawn_as_root` (bool, default true; otherwise instances are children of the spawner node). Released instances are parked inactive and handed out again with `Component::on_pool_reuse`; destroying the spawner destroys its instances. Pool contents and timers are runtime-only and never saved. Covered by `tests/test_spawner.cpp`.

Nodes have a runtime-only `active` flag (`Node::set_active`, default true). It is never written to or read from scene or prefab files: an inactive subtree gets no `on_start`/`on_update`, is not drawn and its physics bodies are disabled (no contacts, triggers or query hits).

## `Tween` component properties

`Tween` is an ordinary component (`"type": "Tween"`) animating one property of its own node in Play.
Properties: `target` (string, default `"position.x"`), `from`, `to` (floats), `use_current_from` (bool; read
`from` from the node when playback starts), `duration`, `delay` (seconds), `easing` (string, default
`"linear"`), `loops` (int, 0 = forever), `yoyo` (bool), `autoplay` (bool, default true). Valid `target`
and `easing` names are listed in `docs/commands.md`. Runtime progress is not saved. Files without a
`Tween` component are unaffected.

## Combat: Health, Hurtbox, Hitbox, Projectile

These components are registered with the physics components. `Health` has `max_health` (float, default 5), `invulnerability_time` (seconds of immunity after each damaging hit, default 0.5), `destroy_on_death` (bool, default false) and `release_on_death` (bool, default true). On death the node is returned to its `Spawner2D` pool if pooled and `release_on_death`; otherwise destroyed (deferred to the end of the tick) if `destroy_on_death`; otherwise it stays dead. Current health, the invulnerability timer and the dead flag are runtime-only, reset in `on_start` and `on_pool_reuse`; C++ code can subscribe with `Health::add_listener` (`HealthEvent` Damaged/Healed/Died) or poll `last_event()`. `Hurtbox` has `damage_multiplier` (float, default 1) and `health_node` (string node name; empty = nearest `Health` on the node or its ancestors) and marks a node with a solid or sensor `Collider2D` as damageable. `Hitbox` has `damage` (float, default 1), `rehit_interval` (seconds before the same target can be hit again while the overlap lasts, default 0.5; 0 = once per overlap), `active` (bool, default true) and `ignore_own_hierarchy` (bool, default true; skips hurtboxes on its own node, ancestors and descendants); it reacts to trigger and collision events of the sibling `Collider2D` (normally a sensor) and applies `damage * damage_multiplier`. `Projectile` has `speed` (float px/s, default 300), `direction` (Vec2, default `[1, 0]`), `lifetime` (seconds, default 2), `pierce` (int, default 0: extra hurtboxes it passes through) and `stop_on_solid` (bool, default true). It drives the sibling `RigidBody2D` velocity every update; use a Dynamic body with `gravity_scale` 0 and a sensor `Collider2D` (Box2D reports sensor overlaps only against non-sensor shapes, so hurtboxes and walls need solid colliders). It despawns (pool release, else node destroy) when the lifetime ends, on touching a solid non-hurtbox collider, or after hitting more than `pierce` hurtboxes; a sibling `Hitbox` deals the damage. **Teams and friendly fire are not properties**: put attackers and targets on different `Collider2D.layer`s and use `mask` so a hitbox only overlaps hurtboxes it may damage (e.g. layer `player_attack` with mask `enemy`). Covered by `tests/test_combat.cpp`.
