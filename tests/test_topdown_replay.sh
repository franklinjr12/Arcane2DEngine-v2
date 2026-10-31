#!/usr/bin/env bash
# Usage: test_topdown_replay.sh <arcane> <arcane_player> <project-dir> <module-file> <strict:0|1>
# GS1: the 60 s arena replay (samples/topdown_slice/tests/arena_60s.replay.json) must match its recorded trace,
# through `arcane test` and through `arcane_player --headless`, with no display.
# The committed trace was recorded with GCC Debug; other compilers/optimisation levels diverge from it (floating
# point, see docs/formats/replay.md), so only the strict (GCC Debug) configuration compares against it. Every
# configuration also checks that two headless runs of the recording agree with each other.
# The trace is only valid for the current arena gameplay. After changing the arena (rules, spawns, components,
# scene data) regenerate it:
#   arcane --project samples/topdown_slice --module <so> run --replay tests/arena_60s.replay.json --trace_out tests/arena_60s.trace.json
set -uo pipefail
ARCANE="$1"; PLAYER="$2"; PROJECT="$3"; MODULE="$4"; STRICT="${5:-0}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
NODISPLAY() { env -u DISPLAY -u WAYLAND_DISPLAY "$@"; }

REC="$PROJECT/tests/arena_60s.replay.json"
if [ "$STRICT" = 1 ]; then
  NODISPLAY "$ARCANE" --project "$PROJECT" --module "$MODULE" test > "$OUT/test.json" 2> "$OUT/test.err" || { cat "$OUT/test.json" "$OUT/test.err" >&2; echo "arcane test failed" >&2; exit 1; }
  grep -Eq '"passed":[1-9]' "$OUT/test.json" || { cat "$OUT/test.json" >&2; echo "no passing replay case" >&2; exit 1; }
fi
NODISPLAY "$PLAYER" --headless --project "$PROJECT" --module "$MODULE" --replay "$REC" --trace-out "$OUT/t.json" \
  || { echo "headless replay failed" >&2; exit 1; }
NODISPLAY "$PLAYER" --headless --project "$PROJECT" --module "$MODULE" --replay "$REC" --expect-trace "$OUT/t.json" \
  || { echo "replay is not deterministic within this build" >&2; exit 1; }
if [ "$STRICT" = 1 ]; then
NODISPLAY "$PLAYER" --headless --project "$PROJECT" --module "$MODULE" \
  --replay "$REC" --expect-trace "$PROJECT/tests/arena_60s.trace.json" \
  || { echo "headless replay diverged (regenerate the trace if the arena changed on purpose)" >&2; exit 1; }
fi
echo "test_topdown_replay OK"
