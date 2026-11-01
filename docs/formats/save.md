# `*.save.json` save game — format `arcane.save`, version 2

Schema: `schemas/arcane.save.schema.json`. Rules shared with all files: `canonical-json.md`.
API: `arcane/save_game.hpp`. Not part of the project (never exported with `assets/`); it lives in a per-user
folder and is plain text, so it can be read and edited without a GUI.

```json
{
  "data_version": 2,
  "format": "arcane.save",
  "values": {
    "level": 3,
    "music": true,
    "player/name": "Ana",
    "volume": 0.75
  },
  "version": 2
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `format` | `"arcane.save"` | required |
| `version` | `2` | required; the file format version |
| `data_version` | integer >= 1 | the game's own save layout version (default 1); compare with `save.data_version` in `project.json` |
| `values` | object | flat key -> boolean, 32-bit integer, binary32 float or string |

Keys are 1-128 characters of `A-Za-z0-9._:/-` (so `player/name` or `inventory.slot1` work as paths); values
keep their JSON kind, so `3` is an integer and `3.0` a float. Unknown fields and other value kinds are errors.

## Location and slots

A slot is a file `<slot>.save.json` (slot: 1-64 of `A-Za-z0-9_-`) in `save_directory(game_id)`:
`$ARCANE_SAVE_DIR/<game_id>` if that variable is set, else `%APPDATA%\Arcane\<game_id>` on Windows or
`$XDG_DATA_HOME/arcane/<game_id>` (default `~/.local/share/arcane/<game_id>`). Commands accept `dir` to
use another folder.

## Atomic writes

`write_save_game` serializes first, writes `<slot>.save.json.arcane-tmp`, closes it and renames it over the
slot (`write_text_file_atomic`). A failure at any step leaves the previous save untouched; a leftover temp file
from a crash is overwritten by the next write and never read.

## Versions and migration

`load_save_game(dir, slot, current_version, migrate)` refuses saves with a higher `data_version` than the game
knows, and upgrades older ones one step at a time by calling `migrate(data, from_version)` for
`from_version, from_version+1, ...` (each call turns data of that layout into the next one). Without a hook an
older save is an error. The file is rewritten only when the game calls `write_save_game`. Without code, the
`save.migrate` command applies one declarative step (rename / remove / defaults) and sets the new version.

## Commands

`save.list`, `save.get`, `save.set`, `save.remove`, `save.migrate` inspect and edit slots;
`save.config_get` / `save.config_set` edit `save` in `project.json` (see `docs/commands.md`).
