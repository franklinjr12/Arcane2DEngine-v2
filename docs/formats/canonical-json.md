# Canonical JSON (all Arcane text formats)

Every engine-authored data file is strict, versioned, canonical JSON. There is no other persistent
format and no opaque data: images, audio and compiled game modules are the only binary files, and
disposable caches may be binary only if they are reproducible and never the sole copy.

Writers (`arcane::canonicalize_json`, `Scene::save_canonical_json`, `save_project_canonical_json`)
always emit exactly this form, so diffs are minimal and re-formatting is idempotent:

- UTF-8, LF line endings, one trailing newline, two-space indentation.
- Object keys sorted bytewise. One member per line. Empty containers are `{}` / `[]`.
- Arrays have one element per line (vectors and colors included). Array order is meaningful and is
  never re-sorted.
- Integers are written as plain decimal digits. IDs (node, component, asset) are unsigned 64-bit
  integers; a JSON consumer in a language with 53-bit numbers must read them as 64-bit integers.
- Every other number is a **binary32 float** written in the shortest decimal form that reads back to
  the same float: fixed notation for decimal exponents -5..15 (always with a `.` fraction, e.g.
  `1.0`, `0.00001`, `123456.79`), otherwise `d[.ddd]e<exp>` (`1e-7`, `1.5e20`). Negative zero is
  written `0.0`. NaN, infinity and values outside binary32 range are errors.
- Strings must be valid UTF-8; only `"`, `\`, and control characters are escaped.

The parser is strict: duplicate object keys, trailing garbage, NaN/Infinity literals and invalid
UTF-8 are rejected (the plain nlohmann parser would silently keep the last duplicate).

## Headers and versions

Every document starts (after sorting: `format` first among the header keys) with
`"format": "arcane.<kind>"` and an integer `"version"`. v2 is the first versioned release of all
kinds: `arcane.project`, `arcane.scene`, `arcane.tileset`, `arcane.clip` and the documents nested in
the project, `arcane.input` and `arcane.hud`. Legacy files carry only `"format_version": 1`.

| Situation | Result |
| --- | --- |
| `format_version: 1`, no `format` | legacy v1 reader (lenient about unknown properties on known component types, as before) |
| `format: "arcane.<kind>"`, `version: 2` | v2 reader, strict |
| `version` greater than supported | error "newer than supported"; never interpreted |
| wrong `format` string, non-integer version, anything else | error |

v2 readers reject unknown fields at every level, unknown component properties, wrongly typed values
(for example `1` for a bool, `1.5` for an int, negative IDs), duplicate node/component IDs (nodes
and components share one counter), unknown component types and project paths that are not
project-relative.

Project-relative paths use `/` only, with no leading `/`, drive letter, empty, `.` or `..` segment.

## Migration

`migrate_project_json`, `migrate_scene_json` and `migrate_project` (`arcane/migration.hpp`) turn
legacy or v2 input into canonical v2. They are strict about legacy files too: an unknown field or
property, an unregistered component type, a future version or malformed input fails the migration
instead of dropping data (including unknown HUD/input/tileset/clip fields, out-of-range input values
that a plain legacy load would clamp, and tile data that does not fit the map). Every result is
verified to reload and re-serialize to the same bytes. The nested `input_actions` and `hud`
documents get their own `format`/`version` header on migration; a project whose header is already
v2 but whose nested documents still say `format_version: 1` (written by the first AF1 build) is
read and upgraded the same way.

`migrate_project` migrates `project.json`, every `*.scene.json` and every tileset/clip file under the
project directory. Tileset and clip files may have any `*.json` name (the samples use `tiles.json`,
`hero_idle.json`), so they are recognized by content (`format` or the legacy keys only that kind
has); files are never renamed and other JSON files are left alone. It
validates everything first, supports a dry run (the report carries old and new text), stages all
changed files as `<name>.arcane-tmp`, commits them, and restores the already-committed files if a
commit fails. Running it on a v2 project changes nothing. The caller supplies a registry containing
the builtin types and the project module's types.

Legacy readers stay as the compatibility path for every kind; there is no removal date.
