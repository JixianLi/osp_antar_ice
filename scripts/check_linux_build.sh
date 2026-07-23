#!/usr/bin/env bash
# Build and render for the Lonestar6 target from a Mac, using Docker.
#
# This runs the %post section extracted verbatim from apptainer/osp_renderer.def
# rather than a hand-copied approximation of it, so the def cannot drift away
# from what is tested. It catches toolchain, fetch-script and RPATH problems
# that a macOS build cannot; it does not reproduce apptainer's --fakeroot user
# namespace, so rpm scriptlet failures still only show up on a real build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEF="$ROOT/apptainer/osp_renderer.def"
SCRATCH="${TMPDIR:-/tmp}/ospr_linux_check"

BASE="$(awk '/^From:/ { print $2; exit }' "$DEF")"
[[ -n "$BASE" ]] || { echo "no From: line in $DEF" >&2; exit 1; }

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/src"
awk '/^%post/ { f = 1; next } /^%[a-z]/ { f = 0 } f' "$DEF" > "$SCRATCH/post.sh"

# ext/ and build/ are platform-specific; the container populates its own.
rsync -aq --exclude .git/ --exclude ext/ --exclude build/ --exclude lib/ --exclude out/ \
    "$ROOT/" "$SCRATCH/src/"

cat > "$SCRATCH/run.sh" <<'INNER'
set -e
bash /post.sh
echo "=== %post OK ==="
cd /src
./scripts/fetch_ext.sh 2>&1 | tail -3
cmake -S . -B build -G Ninja 2>&1 | tail -1
cmake --build build 2>&1 | tail -1
export OSPRAY_NUM_THREADS=4
./build/bin/ospr_render scenes/tetra.json --frame 30 --out /src/out
INNER

docker run --rm --platform linux/amd64 \
    -v "$SCRATCH/src:/src" \
    -v "$SCRATCH/post.sh:/post.sh" \
    -v "$SCRATCH/run.sh:/run.sh" \
    "$BASE" bash /run.sh

echo
echo "rendered frame: $SCRATCH/src/out/frame_00030.png"
