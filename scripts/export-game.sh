#!/usr/bin/env bash
# Exports a self-contained game folder: arcane_player + engine shared library +
# game module + project (scenes, assets). Run it with `arcane_player` inside
# the folder; no editor, no source tree and no install step are needed.
#
# Usage: scripts/export-game.sh [--build-dir DIR] [--mingw] [--project DIR]
#                               [--module FILE] OUTDIR
#   --build-dir  existing or new CMake build dir (default: ./.build-export[-mingw])
#   --mingw      cross-compile for Windows with mingw-w64 (build only; copies
#                the mingw runtime DLLs next to the exe)
#   --project    project dir to export (default: samples/sample_project)
#   --module     game module file (default: the sample_game build output)
# Result: OUTDIR/arcane_player[.exe], OUTDIR/libarcane_runtime.so|arcane_runtime.dll,
#         OUTDIR/game/{project.json,scenes,assets,<module>}
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR=""; MINGW=0; PROJECT="$ROOT/samples/sample_project"; MODULE=""; OUT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --mingw) MINGW=1; shift ;;
    --project) PROJECT="$2"; shift 2 ;;
    --module) MODULE="$2"; shift 2 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) OUT="$1"; shift ;;
  esac
done
[ -n "$OUT" ] || { echo "usage: $0 [options] OUTDIR" >&2; exit 2; }
[ -f "$PROJECT/project.json" ] || { echo "no project.json in $PROJECT" >&2; exit 1; }

CONFIG_ARGS=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DARCANE_BUILD_TESTS=OFF)
if [ "$MINGW" = 1 ]; then
  BUILD_DIR="${BUILD_DIR:-$ROOT/.build-export-mingw}"
  CONFIG_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake")
  EXT=dll
else
  BUILD_DIR="${BUILD_DIR:-$ROOT/.build-export}"
  EXT=so
fi

# An existing build dir is used as configured; only a new one gets configured here.
[ -f "$BUILD_DIR/CMakeCache.txt" ] || cmake -S "$ROOT" -B "$BUILD_DIR" "${CONFIG_ARGS[@]}" >/dev/null
cmake --build "$BUILD_DIR"

rm -rf "$OUT"
mkdir -p "$OUT/game"
cmake --install "$BUILD_DIR" --prefix "$OUT" --component game >/dev/null

# Project data (not the build-artifact module copy that sits in the sample dir).
cp "$PROJECT/project.json" "$OUT/game/"
for d in scenes assets; do
  [ -d "$PROJECT/$d" ] && cp -r "$PROJECT/$d" "$OUT/game/"
done

# Game module: copy under its real platform name and point project.json at it.
[ -n "$MODULE" ] || MODULE="$BUILD_DIR/bin/sample_game.$EXT"
[ -f "$MODULE" ] || { echo "game module not found: $MODULE" >&2; exit 1; }
cp "$MODULE" "$OUT/game/"
sed -i -E "s|(\"module\"[[:space:]]*:[[:space:]]*\")[^\"]*\"|\1$(basename "$MODULE")\"|" "$OUT/game/project.json"

if [ "$MINGW" = 1 ]; then
  # mingw runtime DLLs the exe/engine/module depend on.
  CXX=x86_64-w64-mingw32-g++-posix
  for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
    path="$($CXX -print-file-name=$dll)"
    [ -f "$path" ] && cp "$path" "$OUT/" || echo "warning: $dll not found" >&2
  done
fi

echo "exported to $OUT"
