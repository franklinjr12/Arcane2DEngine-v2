#!/usr/bin/env bash
# Usage: test_export_game.sh <export-game.sh> <build-dir> [project-dir module-file]
# Exports the sample game from an existing build tree into a temp dir and runs
# the exported player from an unrelated cwd with no arguments except
# --frames/--hidden (after a display-free --headless run). Exits 77 (CTest SKIP) if there is no GL display.
set -euo pipefail
SCRIPT="$1"; BUILD="$2"; PROJECT="${3:-}"; MODULE="${4:-}"
EXTRA=()
[ -n "$PROJECT" ] && EXTRA+=(--project "$PROJECT" --module "$MODULE")
MODULE_NAME="$(basename "${MODULE:-sample_game.so}")"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
bash "$SCRIPT" --build-dir "$BUILD" "${EXTRA[@]}" "$OUT/game_export"
E="$OUT/game_export"
for f in arcane_player libarcane_runtime.so game/project.json game/scenes game/assets "game/$MODULE_NAME"; do
  [ -e "$E/$f" ] || { echo "missing in export: $f" >&2; exit 1; }
done
# The player must find libarcane_runtime via $ORIGIN and the project via <exe dir>/game.
cd "$OUT"
env -u DISPLAY -u WAYLAND_DISPLAY "$E/arcane_player" --headless --frames 30
rc=0
"$E/arcane_player" --frames 30 --hidden || rc=$?
exit "$rc"
