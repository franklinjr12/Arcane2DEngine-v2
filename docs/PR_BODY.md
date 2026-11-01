## Franklin: how to test / validate

This PR (`engine-nightly-2026-10-31` -> `engine-nightly-2026-10-30`) is stacked on PR #9 (which is stacked on #8; #7 first). Merge order #7 -> #8 -> #9 -> this one. It shows only tonight's diff (4 commits: IN4, IN5, IN6 and this PR body). Jobs done tonight: IN4 (ENG-034), IN5 (ENG-035), IN6 (ENG-036). Queue is empty after these; IN1-IN3 belong to hermes PR #10.

Verified on the final commit 7e2b867 (WRAP, 2026-11-01): gcc Debug incremental build OK, `ctest` 76/76 passed (perf label 2/2 passed); `arcane --project samples/<p> --module <p>.so run --frames 120` returns `ok:true` for platformer_slice, topdown_slice and sample_project. clang ASan/UBSan and mingw results are from the IN6 job on the same commit (not re-run by WRAP).

**1. Step-by-step manual checks**

Linux:
```
scripts/build-and-test.sh     # expect: gcc PASS, clang_san PASS, mingw PASS (build only)
ctest --test-dir build-gcc -R "test_atomic_save|test_canonical_json|test_migration|test_editor_project|test_tooling_commands" --output-on-failure   # expect: all pass
```
Windows / MSVC (PowerShell, repo root):
```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64     # or "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure       # expect: all tests pass, including test_atomic_save
```
IN6 check: `ctest --test-dir build-gcc -L perf --output-on-failure` (Windows: `ctest --test-dir build -C Debug -L perf`); expect `perf_gate` passed (or skipped on a non-gcc/non-Debug build; it only compares against the committed `Debug-GNU` baseline) and `perf_gate_forced_slow` passed (it expects the gate to fail on 1000x slower timings). Refresh: `cmake --build build-gcc --target perf_baseline_update`.
IN5 check: `ctest --test-dir build-gcc -R test_cli_contract --output-on-failure` (Windows: `ctest --test-dir build -C Debug -R test_cli_contract`); expect pass.
IN4 manual check (any platform): open a project in the editor, change something in the scene and project settings, File > Save. Expected: both saved, no `*.arcane-tmp` or `.arcane-commit` file left in the project folder.

**2. What you can visibly see**
IN6: `docs/perf_gate.md`, `docs/perf_baseline.json`, `ctest -L perf`.
No UI change. New: `engine/include/arcane/atomic_save.hpp` and `test_atomic_save` (a simulated kill after every step of a save). Saves now briefly create a `.arcane-commit` file in the project root.

**3. Enough to merge**
Minimum: the Windows build + ctest above is green (`test_atomic_save` exercises the Windows fsync/rename path). Optional: the editor save check.

**4. Known risks**
- UNTESTED on native Windows/MSVC (only mingw cross-build ran). On Windows `_commit` is used for fsync and directory fsync is skipped.
- Opening a project removes stale `*.arcane-tmp` files under it; a second process saving into the same project at that moment would see its save fail (rolled back, no corruption). Concurrent writers are not supported.
- Single-file saves (`write_text_file_atomic`) are unchanged (rename of a synced-less temp); only the multi-file paths (editor save, CLI save, migration) use the journal. `canonical_json.hpp` still carries the old "not crash-atomic" note on `write_text_files_transactional`, which is now unused by product code (hermes IN2 owns that file tonight).

---

## Technical detail

### Test results per config (tonight's tip 7e2b867)
- gcc Debug headless: ctest 76/76 passed (WRAP, 2026-11-01).
- clang ASan/UBSan headless: PASS in the IN6 job (same tip).
- mingw cross-build: OK in the IN6 job (build only, not run).
- Native Windows/MSVC: not run. UNTESTED.
- Samples (headless `arcane run`, 120 fixed frames): platformer_slice, topdown_slice, sample_project all `ok:true`.

### Delegated decisions tonight
- Branch stacked on `engine-nightly-2026-10-30` (PR #9), not on hermes branches (docs/decisions.md).
- IN6 baseline is per flavor (`Debug-GNU`); other flavors skip the perf gate rather than compare against foreign timings.
- IN4 manifest name `.arcane-commit` and temp suffix `.arcane-tmp` chosen by us; recovery runs at every open.

### UNTESTED on native Windows/MSVC
Everything in this PR: the atomic-save fsync/rename path (`_commit`), the CLI contract test, the perf gate (skips on MSVC) and the new docs. Only the mingw cross-build ran.

### IN6: headless bench regression gate (ENG-036)
- `tests/perf_gate.cmake` (cmake -P) runs `arcane_bench` for scene/particles/physics at default sizes, 5 runs each, median `avg_frame_ms`, fails above baseline +30% (`docs/perf_baseline.json`). Tests `perf_gate` and `perf_gate_forced_slow` (WILL_FAIL, 1000x scale) carry label `perf`; baseline refresh target `perf_baseline_update`. Baseline is for flavor `Debug-GNU`; other flavors skip. Docs in new `docs/perf_gate.md` (hermes owns docs/perf.md and bench_main.cpp tonight, untouched).
- UNTESTED on native Windows/MSVC (the gate skips there anyway: different flavor).

### IN5: CLI exit-code and error contract test (ENG-035)
- New `tests/test_cli_contract.cpp`: for every registry and offline command (generated list) runs `arcane <cmd> --args <invalid>` on a copy of `samples/sample_project`; asserts exit 2, `ok:false` with `code/message/file/path/hint`, `ARGS_INVALID`, single-line stdout, stderr diagnostic, and no project file changed. Also pins `exit_code()` for every code and checks each is listed in its row of `docs/cli-reference.md`. The exit table already existed in the generator, so no generator change.
- Tests: gcc Debug and clang ASan/UBSan green, mingw build OK. UNTESTED on native Windows/MSVC.

### IN4: crash-atomic multi-file save (ENG-034)
- New `arcane/atomic_save.hpp` + `engine/src/atomic_save.cpp`: `commit_files_atomic(root, files, options)` stages every file as `NAME.arcane-tmp` (fwrite + fsync), writes a manifest `ROOT/.arcane-commit` (itself via temp + rename = the commit point), renames the staged files, fsyncs the root, removes the manifest. `recover_pending_commit(root)` rolls a committed-but-unfinished save forward, or discards a missing/torn manifest and all stale `.arcane-tmp`. A kill therefore yields entirely old or entirely new files.
- Ordinary failure while renaming still restores previous bytes and reports `rolled back: ...` (existing tests pass unchanged).
- Call sites: `ProjectSession::save` (editor), `ProjectContext::save` (CLI/tooling), `migrate_project`; recovery runs in `ProjectSession::open` (so editor, player, CLI) and at the start of every commit.
- Test `tests/test_atomic_save.cpp`: crash hook (`crash_at_step`) after each of the 2N+2 steps, recover, assert all-old before the commit point / all-new after, no leftovers; torn manifest; next save recovers; rollback; path outside root.
- Tests: gcc Debug and clang ASan/UBSan green (ctest all passed), mingw cross-build OK.
