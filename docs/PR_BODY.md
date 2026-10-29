## Franklin: how to test / validate

**1. Step-by-step manual checks** (nothing in this PR needs a GUI; the editor/player behave as before)

Linux:
```
scripts/build-and-test.sh          # gcc Debug + clang ASan/UBSan + mingw cross-build; expect gcc PASS and clang PASS
ctest --test-dir .build-and-test/build-gcc -R "canonical|migration|scene_json"   # expect 3/3 passed
```
Windows / MSVC (PowerShell, from the repo root):
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: all tests pass, in particular `test_canonical_json` and `test_migration` (the latter migrates
both sample projects in a temp copy and compares against `tests/golden/*`).

Open a sample in the editor and save it (File > Save): `samples/sample_project/project.json` and the
scene now start with `"format": "arcane.project"` / `"arcane.scene"` and `"version": 2`, keys sorted,
two-space indent. Reopening it works; use `git diff` to see the migrated text, `git checkout samples`
to restore the legacy copies.

**2. What you can visibly see**
- New docs: `docs/formats/canonical-json.md`, `project.md`, `scene.md`; JSON Schemas in `schemas/`.
- Golden migrated files under `tests/golden/` (what a v2 project and scene look like).
- Editor saves now write the canonical v2 format; old files still open.
- No new GUI and no CLI yet (the `arcane` CLI is AF4).

**3. Enough to merge**
- Minimum: Windows/MSVC configure + build + `ctest` green (esp. `test_canonical_json`, `test_migration`).
- Optional: open + save both samples in the editor and run the player on the saved project.

**4. Known risks**
- **UNTESTED on native Windows/MSVC** (only a mingw cross-build ran). Float formatting uses `std::to_chars` (MSVC and libstdc++ both support shortest round-trip); golden byte equality is the test to watch on MSVC.
- `ProjectSession::save` now writes via a temp file + rename; on Windows a locked target file would make a save fail (reported as an error, old file kept).
- Saving a legacy project from the editor rewrites it to v2 (older engine builds cannot read v2). Unknown component properties in legacy files are still dropped silently on an editor save (migration refuses them).

---

# Engine nightly 2026-10-28 — AF1 (draft)

**Base:** master 580b0bf (PR #3 and #4 merged). **Head:** `engine-nightly-2026-10-28`.

## AF1 — canonical project and scene compatibility
- `arcane/canonical_json.hpp`: `canonicalize_json`, `read_text_file`, atomic/staged file writes; internal strict SAX parser (duplicate keys rejected) and canonical writer (`engine/src/canonical_json.*`).
- Scene and project writers emit canonical v2 (`arcane.scene` / `arcane.project`, `version: 2`); readers accept v2 (strict) and legacy v1 (lenient as before). Stricter value typing everywhere (no silent `1.5`→int, no uncaught exceptions on bad types).
- `arcane/migration.hpp`: `migrate_project_json`, `migrate_scene_json`, `migrate_project` (dry run, staged commit, rollback, idempotence verified on every result).
- `ProjectSession::save` serializes before writing and uses atomic writes.
- Docs `docs/formats/*`, schemas `schemas/*.schema.json`; decisions 51–60.
- Tests: `test_canonical_json` (format rules, float policy, malformed/future/unknown), `test_migration` (both samples with their modules: semantic equality, byte-identical repeat, golden files, unknown property/field, future version, malformed, rollback on commit failure, missing module types).

## Test results
`scripts/build-and-test.sh`: gcc Debug PASS (38/38 tests), clang ASan/UBSan PASS (38/38), mingw-w64 cross-build PASS (build only, not run).

## UNTESTED on native Windows/MSVC
Everything above (mingw cross-build only), notably `std::to_chars` float formatting, `std::filesystem::rename` over an existing file, and the golden-file byte comparison.
