#!/usr/bin/env bash
# Populate ext/ with every third-party dependency. ext/ is gitignored; this
# script is the only record of what goes in it.
#
#   ./scripts/fetch_ext.sh              fetch everything missing
#   ./scripts/fetch_ext.sh --record     fetch, then rewrite scripts/ext.sha256
#   ./scripts/fetch_ext.sh --force      re-download even if already present
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXT="$ROOT/ext"
DL="$EXT/.download"
SUMS="$ROOT/scripts/ext.sha256"

# shellcheck source=ext.versions
source "$ROOT/scripts/ext.versions"

RECORD=0
FORCE=0
for arg in "$@"; do
    case "$arg" in
        --record) RECORD=1 ;;
        --force)  FORCE=1 ;;
        *) echo "unknown flag: $arg" >&2; exit 2 ;;
    esac
done

mkdir -p "$DL"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# OSPRay ships one prebuilt archive per (os, arch); pick ours.
case "$(uname -s)/$(uname -m)" in
    Darwin/arm64)   OSPRAY_ASSET="ospray-${OSPRAY_VERSION}.arm64.macosx.zip" ;;
    Darwin/x86_64)  OSPRAY_ASSET="ospray-${OSPRAY_VERSION}.x86_64.macosx.zip" ;;
    Linux/x86_64)   OSPRAY_ASSET="ospray-${OSPRAY_VERSION}.x86_64.linux.tar.gz" ;;
    Linux/aarch64)  OSPRAY_ASSET="ospray-${OSPRAY_VERSION}.aarch64.linux.tar.gz" ;;
    *) echo "no OSPRay binary release for $(uname -s)/$(uname -m)" >&2; exit 1 ;;
esac

OSPRAY_URL="https://github.com/RenderKit/ospray/releases/download/v${OSPRAY_VERSION}/${OSPRAY_ASSET}"
JSON_URL="https://github.com/nlohmann/json/releases/download/v${JSON_VERSION}/json.hpp"
STB_URL="https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/stb_image_write.h"
PUGIXML_URL="https://github.com/zeux/pugixml/releases/download/v${PUGIXML_VERSION}/pugixml-${PUGIXML_VERSION}.tar.gz"
MINIZ_URL="https://github.com/richgel999/miniz/releases/download/${MINIZ_VERSION}/miniz-${MINIZ_VERSION}.zip"
GLFW_URL="https://github.com/glfw/glfw/releases/download/${GLFW_VERSION}/glfw-${GLFW_VERSION}.zip"
IMGUI_URL="https://github.com/ocornut/imgui/archive/refs/tags/v${IMGUI_VERSION}.tar.gz"

download() {
    local url="$1" out="$DL/$2"
    if [[ -f "$out" && $FORCE -eq 0 ]]; then return; fi
    echo "  fetch $2"
    curl -fL --retry 3 --progress-bar -o "$out.part" "$url"
    mv "$out.part" "$out"
}

# Verify against the recorded manifest. Absent manifest means first run.
verify() {
    local name="$1" file="$DL/$1"
    [[ -f "$SUMS" ]] || return 0
    local want
    want="$(awk -v n="$name" '$2 == n { print $1 }' "$SUMS")"
    [[ -n "$want" ]] || return 0
    local got
    got="$(sha256_of "$file")"
    if [[ "$want" != "$got" ]]; then
        echo "checksum mismatch for $name" >&2
        echo "  expected $want" >&2
        echo "  got      $got" >&2
        exit 1
    fi
}

# Each unpack is guarded by a sentinel file inside its own destination rather
# than an external stamp, so a half-finished ext/ heals itself on re-run.
# $1 dest under ext/, $2 sentinel relative to dest, $3 archive, $4 strip-components
unpack_tar() {
    local dest="$EXT/$1"
    if [[ -f "$dest/$2" && $FORCE -eq 0 ]]; then return; fi
    rm -rf "$dest"; mkdir -p "$dest"
    tar -xzf "$DL/$3" -C "$dest" --strip-components="$4"
}

# unzip has no --strip-components; extract to a scratch dir and move the single
# top-level directory into place.
unpack_zip() {
    local dest="$EXT/$1"
    if [[ -f "$dest/$2" && $FORCE -eq 0 ]]; then return; fi
    local scratch="$DL/scratch-$1"
    rm -rf "$scratch" "$dest"; mkdir -p "$scratch"
    unzip -q "$DL/$3" -d "$scratch"
    local top
    top="$(find "$scratch" -mindepth 1 -maxdepth 1 -type d | head -1)"
    mv "$top" "$dest"
    rm -rf "$scratch"
}

echo "==> downloading into ext/.download"
download "$OSPRAY_URL"  "$OSPRAY_ASSET"
download "$JSON_URL"    "json-${JSON_VERSION}.hpp"
download "$STB_URL"     "stb_image_write-${STB_COMMIT}.h"
download "$PUGIXML_URL" "pugixml-${PUGIXML_VERSION}.tar.gz"
download "$MINIZ_URL"   "miniz-${MINIZ_VERSION}.zip"
download "$GLFW_URL"    "glfw-${GLFW_VERSION}.zip"
download "$IMGUI_URL"   "imgui-${IMGUI_VERSION}.tar.gz"

if [[ $RECORD -eq 1 ]]; then
    echo "==> recording checksums into scripts/ext.sha256"
    : > "$SUMS"
    for f in "$DL"/*; do
        [[ -f "$f" ]] || continue
        echo "$(sha256_of "$f")  $(basename "$f")" >> "$SUMS"
    done
    sort -k2 -o "$SUMS" "$SUMS"
else
    echo "==> verifying checksums"
    verify "$OSPRAY_ASSET"
    verify "json-${JSON_VERSION}.hpp"
    verify "stb_image_write-${STB_COMMIT}.h"
    verify "pugixml-${PUGIXML_VERSION}.tar.gz"
    verify "miniz-${MINIZ_VERSION}.zip"
    verify "glfw-${GLFW_VERSION}.zip"
    verify "imgui-${IMGUI_VERSION}.tar.gz"
fi

echo "==> unpacking into ext/"

case "$OSPRAY_ASSET" in
    *.zip)    unpack_zip ospray include/ospray/ospray.h "$OSPRAY_ASSET" 1 ;;
    *.tar.gz) unpack_tar ospray include/ospray/ospray.h "$OSPRAY_ASSET" 1 ;;
esac

mkdir -p "$EXT/json/nlohmann" "$EXT/stb"
cp "$DL/json-${JSON_VERSION}.hpp"        "$EXT/json/nlohmann/json.hpp"
cp "$DL/stb_image_write-${STB_COMMIT}.h" "$EXT/stb/stb_image_write.h"

# pugixml ships a full source tree; we only want the three amalgamation files.
if [[ ! -f "$EXT/pugixml/pugixml.cpp" || $FORCE -eq 1 ]]; then
    unpack_tar pugixml-src src/pugixml.cpp "pugixml-${PUGIXML_VERSION}.tar.gz" 1
    mkdir -p "$EXT/pugixml"
    cp "$EXT/pugixml-src/src/pugixml.cpp"    "$EXT/pugixml/"
    cp "$EXT/pugixml-src/src/pugixml.hpp"    "$EXT/pugixml/"
    cp "$EXT/pugixml-src/src/pugiconfig.hpp" "$EXT/pugixml/"
    cp "$EXT/pugixml-src/LICENSE.md"         "$EXT/pugixml/" 2>/dev/null || true
    rm -rf "$EXT/pugixml-src"
fi

# The miniz release zip is the amalgamated build: miniz.c/miniz.h sit at the
# top level with no enclosing directory, so unpack_zip does not apply.
mkdir -p "$EXT/miniz"
unzip -qo -j "$DL/miniz-${MINIZ_VERSION}.zip" miniz.c miniz.h LICENSE -d "$EXT/miniz"

unpack_zip glfw  include/GLFW/glfw3.h  "glfw-${GLFW_VERSION}.zip"     1
unpack_tar imgui imgui.h               "imgui-${IMGUI_VERSION}.tar.gz" 1

echo
echo "ext/ ready:"
for d in ospray json stb pugixml miniz glfw imgui; do
    printf '  %-10s %s\n' "$d" "$(du -sh "$EXT/$d" 2>/dev/null | cut -f1)"
done
