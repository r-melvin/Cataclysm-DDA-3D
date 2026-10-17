#!/usr/bin/env bash
# Builds tilesets from the I-am-Erk/CDDA-Tilesets sources and installs them into this checkout's
# gfx/ folder, so they show up under Options > Graphics > Choose tileset.
#
#   tools/r3d_spike/compose_tilesets.sh [Tileset ...]
#
# Default: the top-down candidates for the 3D view plus Ultimate Cataclysm as a baseline.
# Needs git and python with pyvips (Arch/CachyOS: sudo pacman -S python-pyvips).
# Environment: TILESETS_SRC (default ../CDDA-Tilesets) is where the sources are cloned.
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
src="${TILESETS_SRC:-$root/../CDDA-Tilesets}"
if [ "$#" -gt 0 ]; then
    sets=( "$@" )
else
    sets=( Retrodays MShockXotto+ NeoDays UltimateCataclysm )
fi

python3 -c "import pyvips" 2>/dev/null || {
    echo "python pyvips is missing (sudo pacman -S python-pyvips)" >&2
    exit 1
}

# Sparse, blob-less clone: only the requested tilesets are downloaded.
if [ ! -d "$src/.git" ]; then
    git clone --depth 1 --filter=blob:none --sparse https://github.com/I-am-Erk/CDDA-Tilesets "$src"
fi
git -C "$src" sparse-checkout add "${sets[@]/#/gfx/}"
git -C "$src" pull --ff-only || true

for name in "${sets[@]}"; do
    in="$src/gfx/$name"
    out="$root/gfx/$name"
    [ -d "$in" ] || { echo "no tileset $name in $src/gfx" >&2; continue; }
    echo "== composing $name"
    rm -rf "$out"
    mkdir -p "$out"
    python3 "$root/tools/gfx_tools/compose.py" "$in" "$out" 2>&1 | grep -v "^WARNING" || true
    cp "$in/tileset.txt" "$out/"
    [ -f "$in/fallback.png" ] && cp "$in/fallback.png" "$out/"
    [ -f "$out/tile_config.json" ] && echo "   installed: $out" || echo "   FAILED: $name" >&2
done
echo "Pick them in game: Options > Graphics > Choose tileset (restart not needed)."
