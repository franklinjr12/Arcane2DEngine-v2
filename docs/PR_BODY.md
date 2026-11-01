## Franklin: how to test / validate

This PR (`engine-nightly-2026-10-31` -> `engine-nightly-2026-10-30`) is stacked on PR #9 (which is stacked on #8; #7 first). Merge order #7 -> #8 -> #9 -> this one. It shows only tonight's diff. Jobs so far: IN4 (ENG-034).

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
IN4 manual check (any platform): open a project in the editor, change something in the scene and project settings, File > Save. Expected: both saved, no `*.arcane-tmp` or `.arcane-commit` file left in the project folder.

**2. What you can visibly see**
No UI change. New: `engine/include/arcane/atomic_save.hpp` and `test_atomic_save` (a simulated kill after every step of a save). Saves now briefly create a `.arcane-commit` file in the project root.

**3. Enough to merge**
Minimum: the Windows build + ctest above is green (`test_atomic_save` exercises the Windows fsync/rename path). Optional: the editor save check.

**4. Known risks**
- UNTESTED on native Windows/MSVC (only mingw cross-build ran). On Windows `_commit` is used for fsync and directory fsync is skipped.
- Opening a project removes stale `*.arcane-tmp` files under it; a second process saving into the same project at that moment would see its save fail (rolled back, no corruption). Concurrent writers are not supported.
- Single-file saves (`write_text_file_atomic`) are unchanged (rename of a synced-less temp); only the multi-file paths (editor save, CLI save, migration) use the journal. `canonical_json.hpp` still carries the old "not crash-atomic" note on `write_text_files_transactional`, which is now unused by product code (hermes IN2 owns that file tonight).

---

## Technical detail

### IN4: crash-atomic multi-file save (ENG-034)
- New `arcane/atomic_save.hpp` + `engine/src/atomic_save.cpp`: `commit_files_atomic(root, files, options)` stages every file as `NAME.arcane-tmp` (fwrite + fsync), writes a manifest `ROOT/.arcane-commit` (itself via temp + rename = the commit point), renames the staged files, fsyncs the root, removes the manifest. `recover_pending_commit(root)` rolls a committed-but-unfinished save forward, or discards a missing/torn manifest and all stale `.arcane-tmp`. A kill therefore yields entirely old or entirely new files.
- Ordinary failure while renaming still restores previous bytes and reports `rolled back: ...` (existing tests pass unchanged).
- Call sites: `ProjectSession::save` (editor), `ProjectContext::save` (CLI/tooling), `migrate_project`; recovery runs in `ProjectSession::open` (so editor, player, CLI) and at the start of every commit.
- Test `tests/test_atomic_save.cpp`: crash hook (`crash_at_step`) after each of the 2N+2 steps, recover, assert all-old before the commit point / all-new after, no leftovers; torn manifest; next save recovers; rollback; path outside root.
- Tests: gcc Debug and clang ASan/UBSan green (ctest all passed), mingw cross-build OK.
