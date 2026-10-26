# Foundation C — minimal integrated editor

## Summary

Finishes Foundation C on top of the already-merged scaffold (`arcane_editor_core`
with `Selection`/`PlayStopController`, PR #1): a working `arcane_editor`
GUI executable, and a standalone `arcane_player --project <dir>` runner.

- **`arcane_editor`** (Dear ImGui docking + GLFW/OpenGL3, `editor/app/`):
  Viewport (active scene rendered into an offscreen FBO via `SpriteRenderer`,
  pan/zoom), Hierarchy (scene tree: select/create/delete/drag-reparent),
  Inspector (transform + a generic walk of the component property
  registry, "Add Component"), Assets (project image files, click to assign
  a `Sprite::texture`), File > Save, and a Play/Stop toolbar backed by
  `PlayStopController`'s snapshot isolation (authored data is restored
  untouched on Stop — never leaks edits made by gameplay code during Play).
  Headless smoke flags: `--project`, `--frames`, `--hidden`, `--play-frames`,
  `--module` (see "Known gaps/pitfalls" below for why `--module` exists).
- **Non-GUI logic lives in `arcane_editor_core`** (static library, testable
  without a GL context): `ProjectSession` (project/scene JSON load+save+dirty
  flag, reusing `project.hpp`'s pure (de)serialization), `AssetCatalog`
  (stable FNV-1a asset ids from relative paths, no asset-database file),
  `ProjectRuntime`/`TextureCache`/`render_scene` (shared project-loading and
  scene-rendering plumbing used by both the editor and the player).
- **Built-in components** (`Sprite`, `Camera`) added to `arcane_runtime` so
  both the editor and a standalone player can render a scene without any
  project-specific module.
- **`arcane_player --project <dir>`**: runs an on-disk project standalone,
  no editor UI — the scene just ticks and renders every frame (no Play/Stop
  mode, the player always acts as "Play"). Shares the same
  `ProjectRuntime`/`render_scene` code as the editor, so behavior is
  identical in both.
- **`samples/sample_project/`**: a minimal project (`project.json` +
  `scenes/main.scene.json` with a `Camera` node and a `Sprite` + the
  existing `sample_game` `Spinner` component on another node, `assets/`
  with a tiny placeholder PNG) used by every headless smoke test below.
- Undo is intentionally **not** implemented — the vision doc treats it as
  "as capability grows," not a Foundation C acceptance item.

## How to build/test

```sh
xvfb-run -a scripts/build-and-test.sh /tmp/a2d-check

# run the editor on the sample project
xvfb-run -a build-gcc/bin/arcane_editor --project samples/sample_project

# run the sample project standalone, no editor UI
xvfb-run -a build-gcc/bin/arcane_player --project samples/sample_project
```

See `README.md`'s new "Editor" section for flags and panel descriptions.

## Test results (this PR, run under `xvfb-run -a`)

| Config | Result |
| ------ | ------ |
| gcc 14 Debug | **PASS** — 16/16 tests (4 `gl`-labeled, run for real via Xvfb/Mesa llvmpipe, not skipped) |
| clang 19 + `ARCANE_SANITIZE=ON` (ASan+UBSan) | **PASS** — 16/16 tests |
| mingw-w64 cross-compile | **PASS** (build only) — `arcane_editor.exe`/`arcane_player.exe` cross-compile cleanly; not run |

New tests this PR: `test_editor_state` (Selection; Play values don't leak
after Stop), `test_editor_project` (registry walk covers builtin + module
component types; `ProjectSession` save/reopen keeps edited property values
— the literal Foundation C acceptance condition; asset ids are stable and
listable), `test_editor_frames` (gl, `arcane_editor --project
samples/sample_project --frames 30 --hidden --play-frames 10`),
`test_player_project_frames` (gl, same project via `arcane_player
--project`).

## Delegated decisions

Full table in `docs/decisions.md` (Foundation C section). Highlights:

- Module reload strategy: rebuild + relaunch, no live hot-swap; Play always
  rebuilds the runtime scene from authored JSON, Stop always discards it.
- Asset ids: FNV-1a 64-bit hash of the path relative to `assets/`, no
  database file — simplest option satisfying save/reopen stability.
- Native module destruction-order hazard: `module` must be declared before
  `registry`/`Scene`/`ProjectSession` in `main()` so `dlclose` happens last;
  getting this backwards segfaults inside `std::function`'s destructor.
  Documented so it doesn't get "simplified" back into a crash.
- Shared source-tree module copy vs. multi-toolchain builds: fixed with an
  explicit `--module` CLI override on both the editor and the player so
  each CTest config loads *its own* build's module, never the shared
  interactive-use copy in `samples/sample_project/`.

## Known gaps / pitfalls

- Undo/redo, multi-select, grid snapping, multi-camera editing: deferred,
  not Foundation C acceptance items.
- **UNTESTED on native Windows/MSVC** — the editor and `--project` player
  mode have only run on Linux (gcc, clang+ASan/UBSan); the mingw target is
  cross-compiled and build-verified only, never executed on real Windows.
- `samples/sample_project/sample_game.so` (the interactive-use copy next to
  the committed sample) is gitignored and rebuilt/overwritten by whichever
  toolchain builds last; don't rely on it matching a specific build — use
  `--module $<TARGET_FILE:sample_game>` for anything that needs a guaranteed
  match (as every CTest entry does).
