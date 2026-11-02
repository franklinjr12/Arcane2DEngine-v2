## Franklin: how to test / validate

This PR (`engine-nightly-2026-11-01` -> `engine-nightly-2026-10-31`) is stacked on PR #11 (merge order #7 -> #8 -> #9 -> #11 -> this). Tonight so far: IN7 (ENG-037, input rebinding + deterministic replay), IN8 (ENG-038, tween easings, chaining, cancel).

**1. Step-by-step manual checks**

Linux: `scripts/build-and-test.sh` (expect gcc PASS, clang_san PASS, mingw build only). Quick: `ctest --test-dir build-gcc -R test_input --output-on-failure`.
Windows / MSVC (PowerShell, repo root):
```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64     # or "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug -R "test_input|test_tween" --output-on-failure   # expect pass
```

**2. What you can visibly see**
No UI change. IN8: `Tween` components accept 7 new easing names (`back_in`, `back_in_out`, `elastic_in|out|in_out`, `bounce_in`, `bounce_in_out`); new `TweenSystem::start_sequence/start_parallel/start_node_sequence/start_node_parallel` and `cancel(id, snap_to_end)`. IN7: new runtime API in `arcane/input.hpp`: `InputActionMap::rebind`, `clear_bindings(name, device)`, `capture_defaults`/`reset_to_defaults`, and `InputRecording`; new cases in `tests/test_input.cpp`.

**3. Enough to merge**
Minimum: Windows build + `test_input` / full ctest green. Optional: nothing manual.

**4. Known risks**
- UNTESTED on native Windows/MSVC (only the mingw cross-build ran).
- IN8: easing endpoints are now exact (`ease(e,0)==0`, `ease(e,1)==1`); `Easing` enumerator values were renumbered (names, which are what files store, are unchanged). `cancel`/`cancel_node` gained a defaulted `snap_to_end` parameter.
- Additive API only; input map file format and loader unchanged.

---

## Technical detail

### IN8: tween easings, chaining, cancel
- Easing set complete: in/out/in-out for quad, cubic, sine, back, elastic, bounce (+linear). Endpoints exact for all.
- Chains: a sequence/parallel group is one `TweenId`; step N+1 of a sequence starts on a computed timeline offset (no drift); per-step `spec.delay` supported; `on_complete` once after the last step. Sequence with a non-last endless step is rejected (returns 0).
- Cancel: stops the rest of a chain, never calls `on_complete`; optional `snap_to_end` applies every step's final value (endless steps skipped); `cancel_node` covers chains bound to the node.
- Tests: `tests/test_tween.cpp` per-easing table (endpoints, clamping, monotonic where expected, golden samples at 0.25/0.5/0.75 computed independently in Python), sequence/parallel timing at fixed dt, cancel/snap/cancel-in-callback/cancel_node tests.
- gcc Debug PASS, clang ASan/UBSan PASS, mingw build PASS. UNTESTED on native Windows/MSVC.

### IN7: input rebinding + deterministic replay
- `rebind(action, binding, allow_conflict=false)`: replaces that device's bindings, reports conflicting actions instead of overwriting.
- `capture_defaults()` / `reset_to_defaults([action])`: in-memory snapshot, not saved to file.
- `InputRecording`: per-tick action states as bytes; 600-tick scripted run recorded twice and replayed gives identical bytes.
