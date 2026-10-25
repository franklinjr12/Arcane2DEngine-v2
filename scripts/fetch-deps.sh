#!/usr/bin/env bash
# Reproduces third_party/<name> from the pinned, checksummed upstream
# archives listed in third_party/MANIFEST.txt. NOT required for a normal
# build: the trimmed sources are already committed under third_party/.
# Set ARCANE_DEPS_CACHE=/dir to use local copies before downloading.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
CACHE="${ARCANE_DEPS_CACHE:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

declare -A URL=(
  [box2d]=https://github.com/erincatto/box2d/archive/refs/tags/v3.2.0.tar.gz
  [miniaudio]=https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
  [imgui]=https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0-docking.tar.gz
  [glfw]=https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.zip
  [stb]=https://github.com/nothings/stb/archive/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5.tar.gz
  [json]=https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
)
declare -A SHA256=(
  [box2d]=ffec747b27a2267016f8d0240d658c82d187fb2271fa0f69fcbe53fbc067180d
  [miniaudio]=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
  [imgui]=746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5
  [glfw]=ea79bc5feffc254c87291980c2d0bce9acebb68c4983b79f961dcd2cb8a611a0
  [stb]=a6c31950b3bdf05eb92258768ba2a11bd44449490574b1aac67028452288c40d
  [json]=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
)
# glad is a generated artifact, not a downloadable release; its pinned,
# checksummed output lives in the deps cache as a tarball (see MANIFEST.txt).
GLAD_SHA256=7d07e60b867033102fa3ea2760634e86b82e5647b4204f8691beb4e973418ce5
GLAD_CACHE_FILE="glad-gl33core-v2.0.8-generated.tar.gz"

CACHE_FILE_OF() {
  case "$1" in
    box2d) echo "box2d-v3.2.0.tar.gz" ;;
    miniaudio) echo "miniaudio-0.11.25.tar.gz" ;;
    imgui) echo "imgui-v1.93.0-docking.tar.gz" ;;
    glfw) echo "glfw-3.5.1.zip" ;;
    stb) echo "stb-003501f.tar.gz" ;;
    json) echo "nlohmann-json-v3.12.0.hpp" ;;
  esac
}

sha256_check() {
  local file="$1" expected="$2"
  local actual
  actual="$(sha256sum "$file" | awk '{print $1}')"
  if [[ "$actual" != "$expected" ]]; then
    echo "sha256 mismatch for $file: expected $expected got $actual" >&2
    exit 1
  fi
}

fetch() {
  local name="$1" dest="$2" expected_sha="$3"
  local cache_name
  cache_name="$(CACHE_FILE_OF "$name")"
  if [[ -n "$CACHE" && -f "$CACHE/$cache_name" ]]; then
    cp "$CACHE/$cache_name" "$dest"
  else
    curl -fL "${URL[$name]}" -o "$dest"
  fi
  sha256_check "$dest" "$expected_sha"
}

TRIM_DIRS=(docs examples tests test samples benchmark .github website research camal extras tools .NO_AI deprecated data)

trim() {
  local dir="$1"
  local args=()
  for d in "${TRIM_DIRS[@]}"; do args+=(-o -name "$d"); done
  find "$dir" -maxdepth 2 -type d \( -false "${args[@]}" \) -exec rm -rf {} +
}

echo "== box2d =="
f="$WORK/box2d.tar.gz"; fetch box2d "$f" "${SHA256[box2d]}"
tar -xzf "$f" -C "$WORK"
rm -rf "$TP/box2d"; mv "$WORK"/box2d-*/ "$TP/box2d"
rm -rf "$TP/box2d/api" "$TP/box2d/extern" "$TP/box2d/shared"
trim "$TP/box2d"

echo "== miniaudio =="
f="$WORK/miniaudio.tar.gz"; fetch miniaudio "$f" "${SHA256[miniaudio]}"
tar -xzf "$f" -C "$WORK"
rm -rf "$TP/miniaudio"; mv "$WORK"/miniaudio-*/ "$TP/miniaudio"
rm -rf "$TP/miniaudio/resources" "$TP/miniaudio/external"
trim "$TP/miniaudio"

echo "== imgui =="
f="$WORK/imgui.tar.gz"; fetch imgui "$f" "${SHA256[imgui]}"
tar -xzf "$f" -C "$WORK"
rm -rf "$TP/imgui"; mv "$WORK"/imgui-*/ "$TP/imgui"
trim "$TP/imgui"
rm -rf "$TP/imgui/misc"
find "$TP/imgui/backends" -maxdepth 1 -type f \
  ! -name 'imgui_impl_glfw.*' ! -name 'imgui_impl_opengl3.*' ! -name 'imgui_impl_opengl3_loader.h' \
  -exec rm -f {} +

echo "== glfw =="
f="$WORK/glfw.zip"; fetch glfw "$f" "${SHA256[glfw]}"
( cd "$WORK" && unzip -q glfw.zip )
rm -rf "$TP/glfw"; mv "$WORK"/glfw-*/ "$TP/glfw"
trim "$TP/glfw"

echo "== stb =="
f="$WORK/stb.tar.gz"; fetch stb "$f" "${SHA256[stb]}"
tar -xzf "$f" -C "$WORK"
rm -rf "$TP/stb"; mv "$WORK"/stb-*/ "$TP/stb"
trim "$TP/stb"

echo "== json =="
f="$TP/json/json.hpp"
mkdir -p "$TP/json"
fetch json "$f" "${SHA256[json]}"

echo "== glad (generated, from cache only) =="
if [[ -n "$CACHE" && -f "$CACHE/$GLAD_CACHE_FILE" ]]; then
  sha256_check "$CACHE/$GLAD_CACHE_FILE" "$GLAD_SHA256"
  rm -rf "$TP/glad"; mkdir -p "$TP/glad"
  tar -xzf "$CACHE/$GLAD_CACHE_FILE" -C "$TP/glad" --strip-components=1
else
  echo "no ARCANE_DEPS_CACHE/$GLAD_CACHE_FILE found; leaving existing third_party/glad in place" >&2
fi

echo "done. Review 'git status' under third_party/ before committing."
