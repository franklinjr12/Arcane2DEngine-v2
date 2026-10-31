# Loader error locations

Every validation error from a format loader says where the problem is:

```
<file>#<pointer>: <message>
scenes/main.json#/nodes/0/id: expected an integer
```

- `<file>` is the project-relative, `/`-separated path of the file that was read.
- `<pointer>` is an [RFC 6901](https://www.rfc-editor.org/rfc/rfc6901) JSON
  pointer to the offending value. `~` is escaped as `~0` and `/` as `~1`
  inside a key. The document root, and any JSON parse error, is a bare `#`
  (`project.json#: project: ...`).
- `<message>` is human-readable and may change; the `<file>#<pointer>` prefix
  is the stable part.

## Text-only loaders and callers

The text-based loader APIs (`load_*_from_json`, `load_*_from_json_strict`,
`migrate_*_json`, `Scene::load_from_json*`) never see a file name, so they
return `#<pointer>: <message>` only. Code that reads a file adds the path with
`arcane::located_error(path, error)` (`arcane/canonical_json.hpp`), which
yields `<path>#<pointer>: <message>` (or `<path>#: <error>` if the error does
not start with `#`). `read_text_file` errors already name the path and are
left unchanged. `arcane::migrate_project`, `ProjectSession::open` and
`load_clip_file` use it.

## Embedded documents

`project.json` embeds the HUD and input-action documents. Their errors are
rebased so the pointer is relative to `project.json`: a problem at `/elements/0`
of the HUD is reported as `project.json#/hud/elements/0`, and one in the input
document as `project.json#/input_actions/...`.

## Audited loaders

| Loader | Example error |
| --- | --- |
| scene | `scenes/main.json#/nodes/0/id: expected an integer` |
| project | `project.json#/name: expected a string` |
| tileset | `assets/tiles.json#/tiles/1/terrain: expected an integer` |
| clip | `assets/walk.json#/frames/0/uv/2: expected a number` |
| hud | `project.json#/hud/elements/0/anchor: ...` (standalone text: `#/elements/0/anchor: ...`) |
| input | `project.json#/input_actions/actions/0/bindings/0/device: unknown input device` |

Prefab, animgraph, replay and save formats do not exist on this branch yet.
When added, their loaders must follow this contract. The atlas loader is out
of scope.

## Enforcement

`tests/test_loader_errors.cpp` is table-driven: each row feeds a bad document
to a loader and checks the pointer and message, and a file-level section
overwrites files in a temporary copy of `tests/golden/sample_project` and
checks that `migrate_project` errors start with `<file>#<pointer>`. Add a row
for every new failure mode.
