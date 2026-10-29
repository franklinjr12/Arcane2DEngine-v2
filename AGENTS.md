# Agent guide

## Repository and builds

The runtime is in `engine/`, the editor and its non-ImGui core in `editor/`,
the player in `player/`, native modules and projects in `samples/`, and CTest
coverage in `tests/`. Public runtime headers are under `engine/include/arcane/`.
See `docs/decisions.md` and `docs/PR_BODY.md` for current decisions and status.

C++23, CMake and a native compiler are required. Dependencies are vendored in
`third_party/`; normal builds do not download anything. To reproduce the pinned
dependencies use `ARCANE_DEPS_CACHE=/workspace/arcaneengine-env/third_party-cache scripts/fetch-deps.sh`.
Do not introduce package-manager or FetchContent downloads. On Linux run:

```sh
scripts/build-and-test.sh
```

This builds and tests GCC Debug and Clang ASan/UBSan with Xvfb when available,
and cross-builds with MinGW if installed. Inspect its summary: the script
reports MinGW failure as a warning. GL-labeled tests require a display or Xvfb;
other tests do not require a window or audio device. Hidden player/editor runs
still create a GL context and are not display-free simulation. On Windows:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Use `"Visual Studio 17 2022"` instead with VS 2022. Run from a Developer
PowerShell, or use the CMake bundled with Visual Studio if `cmake` is not on
PATH. JSON is checked out with LF endings (`.gitattributes`); canonical
comparisons fail on CRLF copies. Native VS 2026 Debug CTest was verified on
2026-10-29; other configurations and visual/audio behavior remain untested.
Preserve shared GLFW and glad,
MSVC-compatible flags and out-of-line special members for pimpl/incomplete-type
owners; see `WINDOWS_FIX.md`.

## Authoring CLI and command registry

The shared command registry is the ImGui-free `arcane_tooling` library (`tooling/`, reference in
`docs/commands.md`), and the `arcane` CLI (AF4B) exposes it: `build-gcc/bin/arcane --project DIR <group> <verb>
[--name value]`, one JSON object on stdout, stable exit codes, `--dry-run`, selectors `id:N`/`path:/A/B`, and
`arcane batch FILE|-` for JSONL scripts. Generated reference: `docs/cli-reference.md` (`arcane docs`;
`test_cli` fails when it drifts, regenerate with `arcane docs > docs/cli-reference.md`). Quickstart:

```sh
arcane project new game --name Demo
arcane --project game node add --name Player            # {"id":1,...}
arcane --project game component add --node id:1 --type Sprite
arcane --project game prop set --node id:1 --component Sprite --name width --value 32.0
arcane --project game validate && arcane --project game fmt --check
```

Pass `--module PATH` to load a native game module from a build tree. Not covered by commands yet: tilemap/tileset
editing, HUD, input maps, clip authoring beyond `atlas make-clip`, asset import, `run`/`export`/`render`/`test`;
the editor still mutates data directly (`docs/editor-command-inventory.md`), so do not claim GUI/CLI parity or
invent commands. The contract remains registry-backed GUI and CLI authoring and JSONL batches, with no MCP or
JSON-RPC server.

Deterministic replay (`docs/formats/replay.md`): `arcane.replay` input recordings and `arcane.replay_trace`
per-frame state hashes, commands `replay.save`/`replay.run`, `arcane_player --replay/--trace-out/--expect-trace/
--screenshot`, and PNG capture with golden comparison (`arcane/screenshot.hpp`, `tests/golden/render/`).
Determinism holds for the same build and platform only; golden images assume Mesa llvmpipe under Xvfb.

New commands go in `tooling/src/`, are registered in a `register_*_commands`
function (called from `register_all_commands`), take a `ProjectContext&`, validate
before mutating, and return the inverse invocation; commands that write files
outside the scene are listed in `register_all_commands` (no `--dry-run`). File-level
commands that must work without an openable project live in `tooling/src/offline.cpp`.
Regenerate `docs/cli-reference.md` in the same commit. Do not keep `Node*`/`Component*`/descriptors past the
`ProjectContext` that produced them: the module is unloaded with the context.

Editor and player can be run from `build-gcc/bin` with `--project` pointing to a
project directory; `--module` overrides its native module path. The samples'
C++ modules register component descriptors through `GameApi::register_types`;
load the module before loading scenes containing its component types, and keep
it alive while its descriptors and instances are in use. See
`samples/sample_game/sample_game.cpp` and `tests/test_migration.cpp`.

## Data and migration

Engine-authored resources use versioned canonical JSON v2; source images, audio,
compiled modules and disposable reproducible caches may be binary. Start with
`docs/formats/canonical-json.md` and the individual format documents in
`docs/formats/`; schemas are in `schemas/`, and migrated examples are under
`tests/golden/`. Project paths must be project-relative as documented there.
Legacy readers remain supported. Explicit migration APIs are in
`engine/include/arcane/migration.hpp`; supply builtin and module registrations.
Test migration on a copy and preserve unknown data: strict migration rejects
unknown fields and properties, while compatible loading can accept legacy data.
Editor save rejects detected unknown data and external edits, but is not a
multi-file transaction or a lock. Keep sample projects readable without requiring
manual migration; verify stable IDs, semantic round trips and canonical output.

For changes, run the relevant tests and `scripts/build-and-test.sh` before
committing. Do not mistake a MinGW cross-build for native Windows testing.
