## Franklin: how to test / validate

This PR (`engine-nightly-2026-11-01` -> `engine-nightly-2026-10-31`) is stacked on PR #11 (merge order #7 -> #8 -> #9 -> #11 -> this). Tonight so far: IN7 (ENG-037, input rebinding + deterministic replay).

**1. Step-by-step manual checks**

Linux: `scripts/build-and-test.sh` (expect gcc PASS, clang_san PASS, mingw build only). Quick: `ctest --test-dir build-gcc -R test_input --output-on-failure`.
Windows / MSVC (PowerShell, repo root):
```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64     # or "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug -R test_input --output-on-failure   # expect pass
```

**2. What you can visibly see**
No UI change. New runtime API in `arcane/input.hpp`: `InputActionMap::rebind`, `clear_bindings(name, device)`, `capture_defaults`/`reset_to_defaults`, and `InputRecording`; new cases in `tests/test_input.cpp`.

**3. Enough to merge**
Minimum: Windows build + `test_input` / full ctest green. Optional: nothing manual.

**4. Known risks**
- UNTESTED on native Windows/MSVC (only the mingw cross-build ran).
- Additive API only; input map file format and loader unchanged.

---

## Technical detail

### IN7: input rebinding + deterministic replay
- `rebind(action, binding, allow_conflict=false)`: replaces that device's bindings, reports conflicting actions instead of overwriting.
- `capture_defaults()` / `reset_to_defaults([action])`: in-memory snapshot, not saved to file.
- `InputRecording`: per-tick action states as bytes; 600-tick scripted run recorded twice and replayed gives identical bytes.
