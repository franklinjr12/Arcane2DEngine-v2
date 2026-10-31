#!/usr/bin/env bash
# Usage: test_render_cli.sh <gl|nodisplay> <arcane> <project-dir> <module-file> <golden.png>
# nodisplay: `arcane render` without a display fails with RENDER_UNAVAILABLE and exit 8 (never crashes, no file written).
# gl: renders the platformer slice after 30 fixed frames, checks the JSON, determinism, the golden (per-channel
# tolerance 2), a failing compare (exit 4) and the mask option. Exits 77 (CTest skip) without a GL display.
set -uo pipefail
MODE="$1"; ARCANE="$2"; PROJECT="$3"; MODULE="$4"; GOLDEN="$5"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp -r "$PROJECT" "$WORK/p" && mkdir -p "$WORK/p/golden" && cp "$GOLDEN" "$WORK/p/golden/f30.png" || exit 1
render() { "$ARCANE" --project "$WORK/p" --module "$MODULE" render "$@" 2>/dev/null; }

if [ "$MODE" = nodisplay ]; then
  out="$(env -u DISPLAY -u WAYLAND_DISPLAY bash -c '"$0" --project "$1" --module "$2" render --output out/x.png 2>/dev/null' "$ARCANE" "$WORK/p" "$MODULE")"; rc=$?
  [ $rc -eq 8 ] || { echo "expected exit 8, got $rc: $out" >&2; exit 1; }
  case "$out" in *'"code":"RENDER_UNAVAILABLE"'*) ;; *) echo "missing RENDER_UNAVAILABLE: $out" >&2; exit 1;; esac
  [ ! -e "$WORK/p/out/x.png" ] || { echo "file written on failure" >&2; exit 1; }
  echo "test_render_cli nodisplay OK"; exit 0
fi

out="$(render --output out/a.png --size 320x180 --frames 30)"; rc=$?
[ $rc -eq 8 ] && { echo "no GL display, skipping" >&2; exit 77; }
[ $rc -eq 0 ] || { echo "render failed ($rc): $out" >&2; exit 1; }
case "$out" in *'"frame":30'*'"scene_hash"'*|*'"scene_hash"'*'"frame":30'*) ;; *) echo "bad metadata: $out" >&2; exit 1;; esac
[ -s "$WORK/p/out/a.png" ] || { echo "no png" >&2; exit 1; }
render --output out/b.png --size 320x180 --frames 30 >/dev/null || exit 1
cmp -s "$WORK/p/out/a.png" "$WORK/p/out/b.png" || { echo "render is not deterministic" >&2; exit 1; }

render --output out/c.png --size 320x180 --frames 30 --compare golden/f30.png --tolerance 2 >/dev/null \
  || { echo "golden compare failed" >&2; exit 1; }
# Edit the scene copy so the picture really changes: a camera that zooms in (also covers --camera).
edit() { "$ARCANE" --project "$WORK/p" --module "$MODULE" "$@" >/dev/null 2>&1; }
edit node add --name Probe || { echo "node add failed" >&2; exit 1; }
edit component add --node path:/Probe --type Camera || { echo "component add failed" >&2; exit 1; }
edit prop set --node path:/Probe --component Camera --name zoom --value 2.0 || { echo "prop set failed" >&2; exit 1; }
render --output out/d.png --size 320x180 --frames 30 --camera path:/Probe --compare golden/f30.png --tolerance 2 >/dev/null; rc=$?
[ $rc -eq 4 ] || { echo "mismatching compare should exit 4, got $rc" >&2; exit 1; }
render --output out/e.png --size 320x180 --frames 30 --camera path:/Probe --compare golden/f30.png --mask '[[0,0,320,180]]' >/dev/null \
  || { echo "full mask should ignore every difference" >&2; exit 1; }
render --output out/f.png --size 160x90 --frames 30 --compare golden/f30.png >/dev/null; rc=$?
[ $rc -eq 4 ] || { echo "size mismatch should exit 4, got $rc" >&2; exit 1; }
# Parallax is authored through prop set and changes the picture (the Sky layer scrolls with the world at factor 1).
edit prop set --node path:/Level/Sky --component ParallaxLayer --name factor --value '[1.0, 1.0]' || { echo "parallax prop set failed" >&2; exit 1; }
render --output out/g.png --size 320x180 --frames 30 --compare golden/f30.png --tolerance 2 >/dev/null; rc=$?
[ $rc -eq 4 ] || { echo "changed parallax factor should change the render (exit 4), got $rc" >&2; exit 1; }
echo "test_render_cli gl OK"
