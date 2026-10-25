#!/usr/bin/env bash
# Configures, builds, and tests Arcane2DEngine V2 across the toolchains
# available on this box: gcc Debug (required), clang + ASan/UBSan (if clang
# is present), and a mingw-w64 cross-compile build-only sanity check (if
# mingw is present; not run, just built). Prints a summary table at the end
# and exits non-zero if the required gcc config fails. A mingw failure is
# reported but does not fail the script.
#
# Usage: scripts/build-and-test.sh [outdir]
#   outdir defaults to ./.build-and-test under the repo root.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="${1:-$ROOT/.build-and-test}"
mkdir -p "$OUTDIR"

declare -A RESULT
declare -A DETAIL
OVERALL_RC=0

run_ctest() {
  if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a ctest --test-dir "$1" --output-on-failure
  else
    ctest --test-dir "$1" --output-on-failure
  fi
}

echo "== gcc Debug (required) =="
GCC_DIR="$OUTDIR/build-gcc"
if cmake -S "$ROOT" -B "$GCC_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug >"$GCC_DIR.configure.log" 2>&1 \
  && cmake --build "$GCC_DIR" >"$GCC_DIR.build.log" 2>&1 \
  && run_ctest "$GCC_DIR" >"$GCC_DIR.ctest.log" 2>&1; then
  RESULT[gcc]="PASS"
  DETAIL[gcc]="$(tail -n1 "$GCC_DIR.ctest.log")"
else
  RESULT[gcc]="FAIL"
  DETAIL[gcc]="see $GCC_DIR.configure.log / .build.log / .ctest.log"
  OVERALL_RC=1
fi
echo "${RESULT[gcc]}: ${DETAIL[gcc]}"

if command -v clang++ >/dev/null 2>&1; then
  echo "== clang + ASan/UBSan =="
  CLANG_DIR="$OUTDIR/build-clang-san"
  if CC=clang CXX=clang++ cmake -S "$ROOT" -B "$CLANG_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARCANE_SANITIZE=ON \
      >"$CLANG_DIR.configure.log" 2>&1 \
    && cmake --build "$CLANG_DIR" >"$CLANG_DIR.build.log" 2>&1 \
    && run_ctest "$CLANG_DIR" >"$CLANG_DIR.ctest.log" 2>&1; then
    RESULT[clang_san]="PASS"
    DETAIL[clang_san]="$(tail -n1 "$CLANG_DIR.ctest.log")"
  else
    RESULT[clang_san]="FAIL"
    DETAIL[clang_san]="see $CLANG_DIR.configure.log / .build.log / .ctest.log"
    OVERALL_RC=1
  fi
  echo "${RESULT[clang_san]}: ${DETAIL[clang_san]}"
else
  RESULT[clang_san]="SKIP"
  DETAIL[clang_san]="clang++ not found"
  echo "== clang + ASan/UBSan == SKIP (clang++ not found)"
fi

if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  echo "== mingw-w64 cross-compile (build only) =="
  MINGW_DIR="$OUTDIR/build-mingw"
  if cmake -S "$ROOT" -B "$MINGW_DIR" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake" \
      -DCMAKE_BUILD_TYPE=Debug -DARCANE_BUILD_TESTS=OFF \
      >"$MINGW_DIR.configure.log" 2>&1 \
    && cmake --build "$MINGW_DIR" >"$MINGW_DIR.build.log" 2>&1; then
    RESULT[mingw]="PASS"
    DETAIL[mingw]="build only, not run"
  else
    RESULT[mingw]="FAIL (warning only)"
    DETAIL[mingw]="see $MINGW_DIR.configure.log / .build.log"
  fi
  echo "${RESULT[mingw]}: ${DETAIL[mingw]}"
else
  RESULT[mingw]="SKIP"
  DETAIL[mingw]="x86_64-w64-mingw32-g++ not found"
  echo "== mingw-w64 cross-compile == SKIP (mingw not found)"
fi

echo
echo "== Summary =="
printf '%-14s %-10s %s\n' "config" "result" "detail"
for cfg in gcc clang_san mingw; do
  printf '%-14s %-10s %s\n' "$cfg" "${RESULT[$cfg]}" "${DETAIL[$cfg]}"
done

exit "$OVERALL_RC"
