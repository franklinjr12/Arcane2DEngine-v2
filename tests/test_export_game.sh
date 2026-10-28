#!/usr/bin/env bash
# Usage: test_export_game.sh <export-game.sh> <build-dir>
# Exports the sample game from an existing build tree into a temp dir and runs
# the exported player from an unrelated cwd with no arguments except
# --frames/--hidden. Exits 77 (CTest SKIP) if there is no GL display.
set -euo pipefail
SCRIPT="$1"; BUILD="$2"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
bash "$SCRIPT" --build-dir "$BUILD" "$OUT/game_export"
E="$OUT/game_export"
for f in arcane_player libarcane_runtime.so game/project.json game/scenes game/assets game/sample_game.so; do
  [ -e "$E/$f" ] || { echo "missing in export: $f" >&2; exit 1; }
done
# The player must find libarcane_runtime via $ORIGIN and the project via <exe dir>/game.
cd "$OUT"
rc=0
"$E/arcane_player" --frames 30 --hidden || rc=$?
exit "$rc"
