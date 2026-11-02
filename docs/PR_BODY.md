## Franklin: how to test / validate

This PR (`engine-nightly-2026-11-01` -> `engine-nightly-2026-10-31`) is stacked on PR #11 (merge order #7 -> #8 -> #9 -> #11 -> this). Tonight so far: IN7 (ENG-037, input rebinding + deterministic replay), IN8 (ENG-038, tween easings, chaining, cancel), IN10 (ENG-040, save slot recovery), IN11 (ENG-041, log level filter + deterministic file sink), IN12 (ENG-042, property registry introspection + round trip), IN13 (ENG-043, canonical_json fuzz + idempotence tests), IN14 (ENG-044, platformer slopes), IN15 (ENG-045, fixed timestep: step clamp counters, alpha in [0,1), time_scale, invalid dt rejected; additive API in time.hpp).

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

IN10: no UI change; `arcane/save_game.hpp` gains `load_save_game_recovering` (falls back to `<slot>.save.json.bak`, reports why), `delete_save_game`, `save_backup_path`; `write_save_game` keeps the previous good file as `.bak` and refuses to overwrite a file from a newer engine. Tests: `ctest -R test_save_game`.
IN11: no UI change; `arcane/log.hpp` gains `log_set_min_level`, `log_set_category_level`, `log_would_log`, `log_add_sink`/`log_remove_sink`, `log_set_stderr_enabled`, `log_flush`, and `log_set_file(path, LogFileOptions{max_bytes,max_files,timestamps})`. File lines are `LEVEL|category|message` (no wall-clock unless enabled). Tests: `ctest -R test_logger`.
IN12: no UI change; `arcane/property.hpp` gains `describe_components(registry)` (types sorted by name, properties in registration order with type and default), `get_property_by_name`/`set_property_by_name` (unknown type/property and wrong value type are errors, data untouched) and `value_fits_type`. `tests/test_property_roundtrip.cpp` sets non-default values on every registered type (built-ins + sample_game), saves, reloads and compares values and bytes. Run `ctest -R test_property_roundtrip`. component_registry.* untouched.
IN13: no UI change; `ctest -R test_canonical_json` now runs 12,000 seeded fuzz iterations (~0.6 s: random nesting, unicode escapes, int64/uint64 extremes, binary32 edge floats, shuffled keys; checks idempotence, value round trip, sorted keys, truncation and random byte damage). One real bug found and fixed in `engine/src/canonical_json.cpp`: deeply nested input (e.g. 200000 `[`) overflowed the stack and crashed; nesting deeper than 512 levels is now an error. Documented in `docs/formats/canonical-json.md`.
IN16 (ENG-046): no UI change; `arcane/animation.hpp` gains runtime-only `AnimationClip::ping_pong` (clip format unchanged), `clip_finished`, `frame_events(clip, from, to)` (frames entered, once per crossing, forward/reverse, multi-frame skips, capped at 64 periods) and `AnimatedSprite::previous_elapsed_seconds`; speed 0 freezes, negative reverses, non-finite advance ignored. Run `ctest -R test_animation`.

IN14: no UI change; `PlatformerController2D` walks up/down slopes (up to `max_slope_deg`, default 50) without sliding or launching, using a downward raycast through the active GameContext's PhysicsWorld. Coyote time / jump buffer / jump cut already existed; new cases in `tests/test_platformer.cpp` (`ctest -R test_platformer`). `max_slope_deg` is a C++ member only, not a serialized property (would alter goldens).

**3. Enough to merge**
Minimum: Windows build + `test_input` / full ctest green. Optional: nothing manual.

**4. Known risks**
- UNTESTED on native Windows/MSVC (only the mingw cross-build ran).
- IN8: easing endpoints are now exact (`ease(e,0)==0`, `ease(e,1)==1`); `Easing` enumerator values were renumbered (names, which are what files store, are unchanged). `cancel`/`cancel_node` gained a defaulted `snap_to_end` parameter.
- Additive API only; input map file format and loader unchanged.
- ENG-039 (text layout) was dropped from this PR: the board row is owned by another agent; the commit is preserved on branch side/eng-039-in9-2026-11-01.

---

## Technical detail

### IN13: canonical_json fuzz
Fixed-seed mt19937_64 generator in `tests/test_canonical_json.cpp`; only `canonicalize_json` is public, so values are emitted as text with random whitespace/escapes/key order and checked against an nlohmann model. A leading UTF-8 BOM is accepted and dropped (nlohmann behaviour, left as is). Write/transaction path untouched.

### IN10: save slot recovery
- `write_save_game`: old file valid -> copied atomically to `.bak` first; corrupt old file never replaces a good `.bak`; newer file `version` is refused untouched. Existing `load_save_game`/`read_save_game_file` and all error texts unchanged.
- No checksum field: the `arcane.save` v2 schema is strict (schemas/* is hermes-owned tonight), so truncation/garbage is detected by the strict parse instead. Future data_version refused by `migrate_save_game` without fallback.
- Tests: `tests/test_save_game.cpp` (list/delete, upgrade, truncated->.bak, garbage, future version, no temp leftovers). gcc PASS, clang ASan/UBSan PASS, mingw build PASS. UNTESTED on native Windows/MSVC.

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
