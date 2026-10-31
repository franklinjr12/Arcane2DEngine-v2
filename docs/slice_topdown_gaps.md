# Top-down arena slice (GS1 v2)

`samples/topdown_slice` is a small Brotato-like arena built from reusable components (`TopDownController2D`,
`SteeringAgent2D` with grid A*, `Health`/`Hurtbox`/`Hitbox`/`Projectile`, pooled `Spawner2D`, `EnemySpawner`,
`CoinSpawner`); `ArenaRules` in `samples/topdown_slice_game/topdown_slice_game.cpp` is only glue. Art is reused placeholder art.

## Controls

| Key | Action |
| --- | --- |
| W A S D | move |
| Space | fire a pooled bullet in the last move direction |
| P (or Escape) | pause / resume |
| R | restart (on the game over screen) |

## Loop

- Red enemies arrive in waves (`wave_duration`, `intermission`, `base_enemies`, `enemies_per_wave` on `ArenaRules`),
  spawned at seeded random points away from the player, and walk around the interior wall to reach the player.
- Contact costs the player health (HUD + particle burst). Bullets kill enemies; killed enemies drop pooled coins
  that the player collects (`pickup` layer); the HUD shows health, wave and time left, and the coin count.
- Pause freezes the live arena nodes and shows PAUSED. At 0 health a GAME OVER screen shows wave and coins; R, or
  `restart_delay` seconds (default 3), restarts the scene and resets the HUD.

## Run it

```sh
build-gcc/bin/arcane_player --project samples/topdown_slice --module build-gcc/bin/topdown_slice_game.so    # needs a display or Xvfb
build-gcc/bin/arcane_player --headless --project samples/topdown_slice --module build-gcc/bin/topdown_slice_game.so \
  --replay samples/topdown_slice/tests/arena_60s.replay.json --expect-trace samples/topdown_slice/tests/arena_60s.trace.json
build-gcc/bin/arcane --project samples/topdown_slice --module build-gcc/bin/topdown_slice_game.so run --frames 600
build-gcc/bin/arcane --project samples/topdown_slice --module build-gcc/bin/topdown_slice_game.so test          # replays tests/*.replay.json
scripts/export-game.sh ...   # exports a game folder; covered by test_export_topdown_slice (needs Xvfb)
```

`tests/arena_60s.replay.json` is a scripted 60 s run (WASD, repeated Space, a short pause). Its
`arena_60s.trace.json` is only valid for the current arena gameplay and build (GCC Debug; `test_topdown_replay_60s` compares against it only in that configuration, other builds just check self-consistency): regenerate it with
`arcane ... run --replay tests/arena_60s.replay.json --trace_out tests/arena_60s.trace.json` whenever the arena's rules,
components or scene data change (see `tests/test_topdown_replay.sh`). Perf numbers: `docs/perf.md`.

## Remaining gaps

- No shop or upgrades between waves.
- No ranged enemies, line of sight, or separation between enemies; no dedicated enemy attack collider (the `enemy_attack` layer is declared but unused).
- No knockback (physics has no impulse API) or melee. Navigation is a uniform grid from solid tiles and ignores moving obstacles.
- No audio.
- Placeholder art only.
- Not validated on native Windows/MSVC or visually by hand; the GL/export checks use Xvfb and Mesa.
- The command registry cannot yet create nodes for the slice from scratch in one step; see `docs/editor-command-inventory.md`.
