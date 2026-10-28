# Arcane2DEngine V2

A lightweight, cross-platform (Windows/Linux) C++23 2D game engine: a shared
runtime library, native C++ game modules, and (eventually) an integrated
Dear ImGui editor. This is a ground-up rewrite; V1 remains unchanged at
https://github.com/franklinjr12/Arcane2DEngine and in this repository's git
history (see the initial commit).

See `docs/decisions.md` for the technical contracts settled for this
foundation milestone, and `docs/PR_BODY.md` for a summary of what this PR
contains and how it was validated.

## Status

This is **Foundation A**: reproducible builds, a minimal window + OpenGL
3.3 core sprite renderer, logging, a fixed-timestep loop helper, a dynamic
game-module loader, and tests. Scene graph, serialization, physics/audio
gameplay wiring, and the editor are later foundations (see the vision doc).

**Windows: CMake-correct and mingw cross-compiled on Linux, but UNTESTED on
native Windows/MSVC.**

## Layout

```
CMakeLists.txt          top-level build
cmake/                  toolchain files and CMake helpers
engine/include/arcane/  public runtime headers
engine/src/             private runtime implementation
player/                 arcane_player standalone launcher
samples/sample_game/    native C++ game module used for validation
tests/                  CTest executables (plain asserts, no framework)
scripts/                dependency fetch + build/test scripts
third_party/            vendored, pinned third-party sources
docs/                   decisions, PR body
```

## Prerequisites

### Linux
- GCC 14+ or Clang 19+, CMake 3.28+, Ninja or Make
- X11/Wayland dev headers for GLFW (`libx11-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev` or your distro's Wayland equivalents), OpenGL
  dev headers (`libgl1-mesa-dev`)
- `xvfb-run` to run the GL-labeled tests without a real display
- Optional: `mingw-w64` (posix threads variant) to cross-compile the Windows
  target from Linux

### Windows
- Visual Studio 2022 17.10+ (MSVC v143, C++23) **or** MinGW-w64 GCC 14
- CMake 3.28+

## Dependencies

All third-party sources are vendored under `third_party/` (trimmed: no
tests/docs/examples/benchmarks) and committed, so a fresh clone builds
offline without running any script. `scripts/fetch-deps.sh` re-derives
`third_party/` from the pinned, checksummed sources in
`third_party/MANIFEST.txt`; it is only needed to reproduce or refresh the
vendored copies, not for a normal build.

```sh
# optional, only to re-fetch/verify third_party/ from scratch
ARCANE_DEPS_CACHE=/path/to/local/cache scripts/fetch-deps.sh
```

## Build and test — Linux

```sh
# gcc, Debug, with tests
cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-gcc
xvfb-run -a ctest --test-dir build-gcc --output-on-failure

# clang + ASan/UBSan
CC=clang CXX=clang++ cmake -S . -B build-clang-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARCANE_SANITIZE=ON
cmake --build build-clang-san
xvfb-run -a ctest --test-dir build-clang-san --output-on-failure

# everything at once (gcc + clang/sanitizers + mingw cross-compile), matches CI gate
scripts/build-and-test.sh /tmp/a2d-check
```

Run the player directly:

```sh
xvfb-run -a build-gcc/bin/arcane_player --frames 60 --hidden
```

## Editor

`arcane_editor` (built alongside everything else, `build-gcc/bin/arcane_editor`)
is a Dear ImGui docking app for authoring a project on disk:

```sh
xvfb-run -a build-gcc/bin/arcane_editor --project samples/sample_project
```

Panels: **Viewport** (renders the active scene into an offscreen
framebuffer, pan/zoom), **Hierarchy** (scene tree — select, create,
delete, drag-and-drop to reparent), **Inspector** (transform + a
property-registry-driven editor for every component on the selected
node, "Add Component"), **Assets** (project image files; click one to
assign it to the selected node's `Sprite`). Toolbar **Play**/**Stop**
run the scene in an isolated runtime copy — authored data is restored
untouched on Stop, see `docs/decisions.md` Foundation C row 9. **File >
Save** writes the project/scene JSON back to disk.

Flags: `--project <dir>` (required), `--frames N` (exit after N frames,
for smoke-testing), `--hidden` (no visible window), `--play-frames N`
(auto-enter Play at startup, tick N frames, then Stop — headless Play/Stop
coverage), `--module <path>` (override the project's module .so/.dll
instead of resolving `project.json`'s `module` field — mainly for build
scripts that need to point at a specific build's own module rather than
the shared source-tree copy, see decisions.md row 23).

`arcane_player --project <dir>` runs the same project standalone, no
editor UI — the scene just ticks and renders every frame (no Play/Stop
mode; the player always acts as if playing). Same `--frames`/`--hidden`/
`--module` flags.

**UNTESTED on native Windows/MSVC**: the editor and `--project` player
mode have only been exercised on Linux (gcc, clang+ASan/UBSan) and
build-only cross-compiled with mingw tonight — nobody has run either on
real Windows yet.

## Physics and input

Add a `RigidBody2D`(+ optional `Collider2D`) component pair to a node (via
the Inspector's "Add Component", or directly in scene JSON) to give it
Box2D-backed simulation while Play is running: `PhysicsWorld` steps the
world every fixed tick, syncs the node's `Transform2D` back, and calls
`Component::on_contact_begin`/`on_contact_end` on shapes that start/stop
touching. All fields stay in pixels (32 px = 1 m internally, see
`docs/decisions.md`); the editor's authored scene in Edit mode is never
simulated.

An `InputActionMap` (named actions bound to keyboard/mouse/gamepad codes,
`held`/`pressed`/`released` queries) is configured per-project as the
`input_actions` object in `project.json` — see
`samples/sample_project/project.json` and `sample_game`'s
`PlayerController` for a worked example. Gameplay code reaches the live
map via `arcane::active_input_map()`, which is non-null only while Play is
actually running.

**UNTESTED on native Windows/MSVC**: physics and input have only been
exercised on Linux (gcc, clang+ASan/UBSan) and build-only cross-compiled
with mingw tonight.

## Build and test — Windows (untested on real hardware)

Developer PowerShell / Visual Studio:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

MinGW/Ninja:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Cross-compiling the mingw target from Linux (build only, no run):

```sh
cmake -S . -B build-mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake -DARCANE_BUILD_TESTS=OFF
cmake --build build-mingw
```

## License

MIT, see `LICENSE.txt`.
