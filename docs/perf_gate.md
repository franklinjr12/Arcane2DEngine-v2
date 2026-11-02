# Headless bench regression gate (IN6)

`ctest -L perf` runs `tests/perf_gate.cmake`, which drives `arcane_bench` (docs/perf.md) with three
default-size cases, five runs each, and compares the **median `avg_frame_ms`** with `docs/perf_baseline.json`:

| Case | `arcane_bench` arguments |
| --- | --- |
| `scene` | `--sprites 1000 --emitters 0 --bodies 0` |
| `particles` | `--sprites 0 --emitters 10 --particles-per-emitter 100 --bodies 0` |
| `physics` | `--sprites 0 --emitters 0 --bodies 200` |

A case fails when its median is more than the entry's `threshold_percent` above the baseline. No renderer is involved.

Timings only compare on the same kind of build and machine, so `docs/perf_baseline.json` (format version 2) holds one
entry per `<flavor>/<arch>` key. The flavor is `<CMAKE_BUILD_TYPE>-<compiler id>[-san]` (e.g. `Debug-GNU`); the
architecture is `CMAKE_SYSTEM_PROCESSOR` (`-DARCH=`), lowercased with `amd64`/`arm64` mapped to `x86_64`/`aarch64`.
The committed `Debug-GNU/x86_64` entry came from the 8-vCPU dev VM and keeps its 30% threshold; new entries use 20%.

When no entry exists for the current key (new architecture, clang, sanitizers, Release, MSVC), `perf_gate` records one
into the file, prints a `WARNING: perf gate: no baseline for <key>; recorded one ...` and passes; review and commit
the new entry. `perf_gate_forced_slow` sets `FORCE=ON` (compare against another arch of the same flavor, else the
first entry, instead of recording) and multiplies the measurements by 1000 through `ARCANE_PERF_GATE_SCALE`; it must
fail (`WILL_FAIL`), which proves the gate can fail on every flavor. `ARCANE_PERF_GATE_SCALE_PERCENT=P` scales
measurements by P/100 afterwards.

`perf_gate_threshold` (`tests/perf_gate_test/`) is a fast deterministic test: a fake bench that always reports 1 ms,
a temporary baseline with a 20% threshold, +19% must pass, +21% and +25% must fail, and a missing arch must be recorded
with a warning without touching the existing entry.

## Refresh the baseline

After an intended speed change, or on a different machine or build type, from a quiet machine:

```sh
cmake --build build-gcc --target perf_baseline_update
git diff docs/perf_baseline.json   # review, then commit it with the change that explains it
ctest --test-dir build-gcc -L perf
```

The target merges only the current `<flavor>/<arch>` entry (keeping its threshold) and leaves the others alone. Run `ctest -L perf` serially (the tests are `RUN_SERIAL`); heavy parallel
load makes the medians noisy.
