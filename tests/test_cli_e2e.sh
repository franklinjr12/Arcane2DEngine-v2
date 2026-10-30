#!/usr/bin/env bash
# Usage: test_cli_e2e.sh <arcane> <arcane_player> <sample_game-module> <cmake> <build-dir> <sample-project-dir>
# End-to-end, display-free: author a project with the CLI, run/test it headless, install the runtime,
# `arcane export` it, reopen the export and run the exported player headless from another cwd. The
# exported player's trace must equal the `arcane run` trace. Then the same for the sample project.
set -uo pipefail
ARCANE="$1"; PLAYER="$2"; MODULE="$3"; CMAKE="$4"; BUILD="$5"; SAMPLE="$6"
unset DISPLAY WAYLAND_DISPLAY
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { "$@" >"$W/last.json" 2>"$W/last.err" || { cat "$W/last.json" "$W/last.err" >&2; fail "$*"; }; }

cd "$W"
ok "$ARCANE" project new game --name Demo
G=(--project "$W/game")
ok "$ARCANE" "${G[@]}" node add --name Player
ok "$ARCANE" "${G[@]}" component add --node id:1 --type Sprite
ok "$ARCANE" "${G[@]}" prop set --node id:1 --component Sprite --name width --value 32.0
ok "$ARCANE" "${G[@]}" validate
ok "$ARCANE" "${G[@]}" fmt --check
ok "$ARCANE" "${G[@]}" fmt --check
ok "$ARCANE" "${G[@]}" run --frames 120
ok "$ARCANE" "${G[@]}" replay save --file tests/walk.replay.json --frames 60
ok "$ARCANE" "${G[@]}" run --replay tests/walk.replay.json --trace_out tests/walk.trace.json
ok "$ARCANE" "${G[@]}" test
grep -q '"ok": *true' last.json || fail "arcane test not ok: $(cat last.json)"

ok "$CMAKE" --install "$BUILD" --prefix "$W/rt" --component game
ok "$ARCANE" "${G[@]}" export --out "$W/out" --runtime_dir "$W/rt"
for f in arcane_player libarcane_runtime.so game/project.json game/scenes/main.scene.json; do
  [ -e "$W/out/$f" ] || fail "missing in export: $f"
done
[ -e "$W/out/game/tests" ] || [ -e "$W/out/game/tests/walk.replay.json" ] && fail "tests/ must not be exported"
ok "$ARCANE" --project "$W/out/game" validate

mkdir -p "$W/elsewhere" && cd "$W/elsewhere"
env -u DISPLAY -u WAYLAND_DISPLAY "$W/out/arcane_player" --headless --frames 30 || fail "exported player --frames 30"
cp "$W/game/tests/walk.replay.json" "$W/elsewhere/walk.replay.json"
env -u DISPLAY -u WAYLAND_DISPLAY "$W/out/arcane_player" --headless --replay walk.replay.json --trace-out t.json \
  || fail "exported player --replay"
cmp -s t.json "$W/game/tests/walk.trace.json" || fail "exported player trace differs from the arcane run trace"

# A non-empty out needs --overwrite; the failed export leaves it as it was.
cd "$W"
"$ARCANE" "${G[@]}" export --out "$W/out" --runtime_dir "$W/rt" >/dev/null 2>&1 && fail "export over a non-empty out should fail"
[ -e "$W/out/arcane_player" ] || fail "failed export damaged out"
ok "$ARCANE" "${G[@]}" export --out "$W/out" --runtime_dir "$W/rt" --overwrite true
ok "$ARCANE" "${G[@]}" --dry-run export --out "$W/dry" --runtime_dir "$W/rt"
[ -e "$W/dry" ] && fail "dry run wrote out"

# The sample project with its native module.
S=(--project "$SAMPLE" --module "$MODULE")
ok "$ARCANE" "${S[@]}" export --out "$W/sample_out" --runtime_dir "$W/rt"
MODNAME="$(basename "$MODULE")"
for f in arcane_player game/project.json game/scenes "game/$MODNAME"; do
  [ -e "$W/sample_out/$f" ] || fail "missing in sample export: $f"
done
ok "$ARCANE" --project "$W/sample_out/game" --module "$W/sample_out/game/$MODNAME" validate
cd "$W/elsewhere"
env -u DISPLAY -u WAYLAND_DISPLAY "$W/sample_out/arcane_player" --headless --frames 30 || fail "sample export --headless"

# A missing module fails and creates nothing.
cd "$W"
"$ARCANE" --project "$SAMPLE" --module "$W/nope.so" export --out "$W/missing_out" --runtime_dir "$W/rt" >/dev/null 2>&1 \
  && fail "export with a missing module should fail"
[ -e "$W/missing_out" ] && fail "failed export left out behind"
ls -d "$W"/missing_out.tmp-* >/dev/null 2>&1 && fail "failed export left a temp dir"
echo "test_cli_e2e OK"
