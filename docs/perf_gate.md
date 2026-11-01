# Headless bench regression gate (IN6)

`ctest -L perf` runs `tests/perf_gate.cmake`, which drives `arcane_bench` (docs/perf.md) with three
default-size cases, five runs each, and compares the **median `avg_frame_ms`** with `docs/perf_baseline.json`:

| Case | `arcane_bench` arguments |
| --- | --- |
| `scene` | `--sprites 1000 --emitters 0 --bodies 0` |
| `particles` | `--sprites 0 --emitters 10 --particles-per-emitter 100 --bodies 0` |
| `physics` | `--sprites 0 --emitters 0 --bodies 200` |

A case fails when its median is more than `threshold_percent` (30) above the baseline. No renderer is involved.

Timings only compare on the same kind of build, so the baseline records a `flavor`
(`<CMAKE_BUILD_TYPE>-<compiler id>[-san]`; the committed one is `Debug-GNU`, gcc Debug on the 8-vCPU dev VM). On any
other flavor (clang, sanitizers, Release, MSVC) `perf_gate` is reported as skipped (exit 77) instead of failing.
`perf_gate_forced_slow` multiplies the measurements by 1000 through `ARCANE_PERF_GATE_SCALE` and must fail
(`WILL_FAIL`), which proves the gate can fail on every flavor.

## Refresh the baseline

After an intended speed change, or on a different machine or build type, from a quiet machine:

```sh
cmake --build build-gcc --target perf_baseline_update
git diff docs/perf_baseline.json   # review, then commit it with the change that explains it
ctest --test-dir build-gcc -L perf
```

To keep a baseline for another flavor, configure that build and run the same target; it overwrites the file, so
only one flavor is committed at a time. Run `ctest -L perf` serially (the tests are `RUN_SERIAL`); heavy parallel
load makes the medians noisy.
