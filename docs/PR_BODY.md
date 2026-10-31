## Franklin: how to test / validate

This PR (`engine-nightly-2026-10-30` -> `engine-hermes-2026-10-30`) is stacked on PR #8. Merge #7 and #8 first, then this one (retarget to master afterwards). It shows only tonight's diff. Jobs are appended per job below; so far only HY1 (hygiene) has run.

**1. Step-by-step manual checks**

Linux:
```
scripts/build-and-test.sh     # expect: gcc PASS, clang_san PASS, mingw PASS (build only)
git ls-files samples | grep -E '\.(so|dll)$'    # expect: no output
```
Windows / MSVC (PowerShell, repo root):
```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64     # or "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure       # expect: all tests pass
```
After a build, `samples/topdown_slice/topdown_slice_game.so` (or `.dll`) appears in the work tree but `git status` stays clean.

**2. What you can visibly see**

HY1 only: no feature change. The 1.4 MB build artefact `samples/topdown_slice/topdown_slice_game.so` is no longer tracked, and `.gitignore` covers it (and the `.dll`).

**3. Enough to merge**

Minimum: `scripts/build-and-test.sh` (or the Windows ctest above) is green. Optional: open `arcane_editor --project samples/topdown_slice` and confirm it still loads (the module is rebuilt by CMake).

**4. Known risks**

- UNTESTED on native Windows/MSVC for this PR (only the mingw cross-build ran).
- The `.so` stays in the history of PR #7/#8 (commits 49962be, 2e78c85, eb931a1); history was not rewritten, so the repo size is unchanged. The file is only removed from the index.
- Anyone who relied on the committed `.so` must build once so CMake copies it in.

---

## Technical detail

### HY1: branch sanity, build hygiene, PR body
- Tip ac03c08 builds and passes `scripts/build-and-test.sh`: gcc Debug PASS, clang ASan/UBSan PASS, mingw cross-build PASS (build only, not run).
- The old FE18 wip problem (Color `operator==` in prefab.cpp) is fixed at the tip: `Color` has a defaulted `operator==` in `engine/include/arcane/renderer.hpp`.
- `git rm --cached samples/topdown_slice/topdown_slice_game.so`; `.gitignore` gets `/samples/topdown_slice/topdown_slice_game.so` and `.dll`. Other sample modules were already ignored.
- `docs/decisions.md`: row for the branch choice.
