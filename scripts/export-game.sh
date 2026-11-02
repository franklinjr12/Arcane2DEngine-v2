#!/usr/bin/env bash
# Exports a self-contained game folder: arcane_player + engine shared library +
# game module + project (scenes, assets). Run it with `arcane_player` inside
# the folder; no editor, no source tree and no install step are needed.
# The data half is `arcane export` (docs/commands.md): this script builds, installs the
# runtime files into a temp dir and hands both to the CLI.
#
# Usage: scripts/export-game.sh [--build-dir DIR] [--mingw] [--cli PATH] [--project DIR]
#                               [--module FILE] OUTDIR
#   --build-dir  existing or new CMake build dir (default: ./.build-export[-mingw])
#   --mingw      cross-compile for Windows with mingw-w64 (build only; copies
#                the mingw runtime DLLs next to the exe)
#   --cli        native `arcane` used with --mingw, because the Windows arcane.exe cannot run
#                here and cannot load the .dll module, so the project is not opened
#                (default: build-gcc/bin/arcane)
#   --project    project dir to export (default: samples/sample_project)
#   --module     game module file (default: the sample_game build output)
# Result: OUTDIR/arcane_player[.exe], OUTDIR/libarcane_runtime.so|arcane_runtime.dll,
#         shared GLFW and glad libraries, OUTDIR/game/{project.json,scenes,assets,<module>}
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR=""; MINGW=0; CLI=""; PROJECT="$ROOT/samples/sample_project"; MODULE=""; OUT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --mingw) MINGW=1; shift ;;
    --cli) CLI="$2"; shift 2 ;;
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

# Native builds use the freshly built CLI; the mingw build cannot run its own.
VALIDATE=true
if [ "$MINGW" = 1 ]; then
  ARCANE="${CLI:-$ROOT/build-gcc/bin/arcane}"
  VALIDATE=false
else
  ARCANE="${CLI:-$BUILD_DIR/bin/arcane}"
fi
[ -x "$ARCANE" ] || { echo "arcane CLI not found: $ARCANE" >&2; exit 1; }

[ -n "$MODULE" ] || MODULE="$BUILD_DIR/bin/sample_game.$EXT"

RUNTIME="$(mktemp -d)"
trap 'rm -rf "$RUNTIME"' EXIT
cmake --install "$BUILD_DIR" --prefix "$RUNTIME" --component game >/dev/null
"$ARCANE" --project "$PROJECT" --module "$MODULE" export --out "$OUT" --runtime_dir "$RUNTIME" \
  --overwrite true --validate "$VALIDATE" >/dev/null

if [ "$MINGW" = 1 ]; then
  # mingw runtime DLLs the exe/engine/module depend on.
  CXX=x86_64-w64-mingw32-g++-posix
  for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
    path="$($CXX -print-file-name=$dll)"
    [ -f "$path" ] && cp "$path" "$OUT/" || echo "warning: $dll not found" >&2
  done
fi

echo "exported to $OUT"
