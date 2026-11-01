#!/usr/bin/env bash
# Usage: test_headless_player.sh <arcane_player> <project-dir> <module-file> <replay-project-dir> <replay-module> <recording>
# arcane_player --headless runs without a display: --frames works with DISPLAY unset, two runs give byte-identical
# traces, missing --frames/--replay exits 2, and a replay matches a trace recorded by the same headless mode.
set -uo pipefail
PLAYER="$1"; PROJECT="$2"; MODULE="$3"; RPROJECT="$4"; RMODULE="$5"; REC="$6"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
headless() { env -u DISPLAY -u WAYLAND_DISPLAY "$PLAYER" --headless "$@"; }

headless --project "$PROJECT" --module "$MODULE" --frames 120 --trace-out "$OUT/a.json" || { echo "run a failed" >&2; exit 1; }
headless --project "$PROJECT" --module "$MODULE" --frames 120 --trace-out "$OUT/b.json" || { echo "run b failed" >&2; exit 1; }
[ -s "$OUT/a.json" ] && cmp -s "$OUT/a.json" "$OUT/b.json" || { echo "traces differ or are empty" >&2; exit 1; }

headless --project "$PROJECT" --module "$MODULE"; rc=$?
[ $rc -eq 2 ] || { echo "--headless without --frames/--replay should exit 2, got $rc" >&2; exit 1; }
headless --project "$PROJECT" --module "$MODULE" --frames 5 --screenshot "$OUT/x.png"; rc=$?
[ $rc -eq 2 ] || { echo "--headless --screenshot should exit 2, got $rc" >&2; exit 1; }

headless --project "$RPROJECT" --module "$RMODULE" --replay "$REC" --trace-out "$OUT/r.json" || { echo "replay failed" >&2; exit 1; }
headless --project "$RPROJECT" --module "$RMODULE" --replay "$REC" --expect-trace "$OUT/r.json" || { echo "replay diverged" >&2; exit 1; }
echo "test_headless_player OK"
