# Foundation A — reproducible project and runtime

## Summary

Lays down V2 of Arcane2DEngine at the repo root: a C++23/CMake build, an
`arcane_runtime` shared library (logging, a minimal GLFW+OpenGL 3.3 core
window/renderer, a fixed-timestep loop helper, a cross-platform dynamic
game-module loader), a native `sample_game` module loaded through that
boundary, an `arcane_player` executable that runs it, and a CTest suite
covering both headless logic and real (offscreen) GL rendering. Box2D,
miniaudio, Dear ImGui (docking, GLFW+OpenGL3 backends), and nlohmann-json
are vendored and build-verified now, ahead of the gameplay/editor work that
will use them.

The copied V1 engine files at the repo root have been removed in a
dedicated commit; V1 is unaffected and remains available in this
repository's git history and at `franklinjr12/Arcane2DEngine`.

## How to build/test

```sh
# everything this PR is validated against (gcc required, clang+sanitizers,
# mingw cross-compile build-only), matches what CI would run
xvfb-run -a scripts/build-and-test.sh /tmp/a2d-check

# or just the required config
cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-gcc
xvfb-run -a ctest --test-dir build-gcc --output-on-failure

# run the player
xvfb-run -a build-gcc/bin/arcane_player --frames 60 --hidden
```

See `README.md` for the full prerequisites and per-OS instructions.

## Test results (this PR, run under `xvfb-run -a`)

| Config | Result | Detail |
| ------ | ------ | ------ |
| gcc 14 Debug | **PASS** | 9/9 tests, including the 2 `gl`-labeled tests (not skipped — real offscreen GL context via Xvfb/Mesa llvmpipe) |
| clang 19 + `ARCANE_SANITIZE=ON` (ASan+UBSan) | **PASS** | 9/9 tests; GL-labeled tests run with `ASAN_OPTIONS=detect_leaks=0` to avoid a false positive from Mesa llvmpipe's JIT-compiled shader code (see `docs/decisions.md` #19) |
| mingw-w64 cross-compile | **PASS** (build only) | Toolchain file `cmake/toolchains/mingw-w64-x86_64.cmake`; not run (no Windows host here) |

Covered by the 9 tests: logger (levels/format/file sink), fixed-timestep
accumulator logic, dynamic module load of `sample_game` + API-version
mismatch rejection, JSON round trip, headless Box2D world step, miniaudio
null-backend context init, ImGui context create/destroy (no GL), a real GL
renderer draw-and-read-back-pixels check, and `arcane_player --frames 30
--hidden` exiting 0.

## Delegated decisions

Full table and rationale in `docs/decisions.md`. Highlights:

- Fixed timestep: 60 Hz, 5-step catch-up cap, render decoupled from physics step.
- Pixel/physics units: 32 px = 1 m, documented ahead of the Box2D↔renderer wiring that will need it.
- Module ABI: a single versioned plain-C entry point (`arcane_get_game_api`), flat POD struct, no C++ ABI crossing the boundary.
- Box2D v3.2.0 (available in the pinned cache tonight; newer than the doc's v3.1.1 mention).
- Dependency policy: vendored + checksummed, committed trimmed sources, offline-buildable; `scripts/fetch-deps.sh` only reproduces/refreshes them.
- Repo layout: V2 at the repo root per Franklin's 22:46 BRT scope update; V1 files removed in one commit, V1 preserved in history and its own repo.

## Known gaps

- No scene graph, property registry, JSON schema, editor, or Play/Stop isolation yet — those are explicitly out of scope for Foundation A (see "Technical contracts still to settle" in `docs/decisions.md`, items left "open (not needed for Foundation A)").
- Box2D/miniaudio/ImGui are build-verified but not yet wired into the renderer/game-loop beyond headless smoke tests.
- No GitHub Actions or other CI config is included by design (per the job brief); `scripts/build-and-test.sh` is what a future CI step (or a human) should run.
- **UNTESTED on native Windows/MSVC (mingw-w64 cross-compile only).**
