#!/usr/bin/env bash
# Usage: test_platformer_replay.sh <arcane_player> <project-dir> <module-file> <strict:0|1>
# GS2: the platformer replay (samples/platformer_slice/tests/level2_run.replay.json) runs level 1 (coins, both
# checkpoints, the goal), switches to level 2 through the SceneManager and falls into a pit there (respawn at its
# checkpoint). Only the windowed player owns a SceneManager (`--headless` does not), so this runs `arcane_player
# --hidden` and needs GL: exits 77 (CTest SKIP) without it.
# The committed trace was recorded with GCC Debug; other compilers/optimisation levels diverge from it (floating
# point, see docs/formats/replay.md), so only the strict (GCC Debug) configuration compares against it. Every
# configuration also checks that two runs of the recording agree with each other.
# The trace is only valid for the current platformer gameplay. After changing the slice (rules, scenes, components)
# regenerate it (under xvfb-run -a when there is no display):
#   arcane_player --hidden --project samples/platformer_slice --module <so> \
#     --replay samples/platformer_slice/tests/level2_run.replay.json --trace-out samples/platformer_slice/tests/level2_run.trace.json
set -uo pipefail
PLAYER="$1"; PROJECT="$2"; MODULE="$3"; STRICT="${4:-0}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
REC="$PROJECT/tests/level2_run.replay.json"
run() { "$PLAYER" --hidden --project "$PROJECT" --module "$MODULE" --replay "$REC" "$@"; }

run --trace-out "$OUT/a.trace.json"; rc=$?
[ $rc -eq 77 ] && exit 77
[ $rc -eq 0 ] || { echo "replay failed ($rc)" >&2; exit 1; }
run --expect-trace "$OUT/a.trace.json" || { echo "replay is not deterministic within this build" >&2; exit 1; }
if [ "$STRICT" = 1 ]; then
  run --expect-trace "$PROJECT/tests/level2_run.trace.json" \
    || { echo "replay diverged (regenerate the trace if the platformer slice changed on purpose)" >&2; exit 1; }
fi
echo "test_platformer_replay OK"
