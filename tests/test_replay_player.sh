#!/usr/bin/env bash
# Usage: test_replay_player.sh <arcane_player> <project-dir> <module-file> <recording>
# Replays a recording through the player twice: the second run must match the first run's trace (exit 0), a
# changed recording must diverge (exit 3), and --screenshot must leave a PNG. Exits 77 (CTest SKIP) without GL.
set -uo pipefail
PLAYER="$1"; PROJECT="$2"; MODULE="$3"; REC="$4"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
run() { "$PLAYER" --hidden --project "$PROJECT" --module "$MODULE" "$@"; }

run --replay "$REC" --trace-out "$OUT/a.trace.json" --screenshot "$OUT/a.png"; rc=$?
[ $rc -eq 77 ] && exit 77
[ $rc -eq 0 ] || { echo "first replay failed ($rc)" >&2; exit 1; }
[ -s "$OUT/a.trace.json" ] && [ -s "$OUT/a.png" ] || { echo "missing trace or screenshot" >&2; exit 1; }
[ "$(head -c 8 "$OUT/a.png" | od -An -tx1 | tr -d ' \n')" = "89504e470d0a1a0a" ] || { echo "not a PNG" >&2; exit 1; }

run --replay "$REC" --expect-trace "$OUT/a.trace.json" || { echo "second replay diverged" >&2; exit 1; }

# Same recording without the first key press: must diverge.
sed 's/"down": true/"down": false/' "$REC" > "$OUT/changed.replay.json"
run --replay "$OUT/changed.replay.json" --expect-trace "$OUT/a.trace.json"; rc=$?
[ $rc -eq 3 ] || { echo "changed input should diverge with exit 3, got $rc" >&2; exit 1; }
echo "test_replay_player OK"
