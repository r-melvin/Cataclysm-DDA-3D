#!/usr/bin/env bash
# Saves a side-by-side comparison of the same turn: the 3D view through an orthographic camera
# at the isometric angle, and the flat view drawn with an isometric tileset.
#
#   tools/r3d_spike/compare_iso.sh [world] [out_prefix]
#
# Loads the first character saved in [world] (default: the most recently played world),
# captures <out_prefix>_3d.png and <out_prefix>_2d.png, writes <out_prefix>.png with both side
# by side (needs ImageMagick; skipped otherwise) and exits. Nothing in the options changes.
#
# Tilesets: the 3D shot uses TILESET_3D (default UltimateCataclysm), the 2D one COMPARE_TILESET
# (default Ultica_iso); missing ones are built into gfx/ with compose_tilesets.sh first.
# Environment: CAMERA (default -45,30,20,1 = yaw,pitch,distance,ortho), TILESET_3D,
# COMPARE_TILESET, USERDIR (where save/ is; default this checkout, the build's default).
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
userdir="${USERDIR:-$root}"
tileset_3d="${TILESET_3D:-UltimateCataclysm}"
tileset_2d="${COMPARE_TILESET:-Ultica_iso}"

world="${1:-}"
if [ -z "$world" ]; then
    # the most recently played world
    latest="$(ls -td "$userdir"/save/*/ 2>/dev/null | head -1)"
    [ -n "$latest" ] || {
        echo "no saved game in $userdir/save: start the game, New Game > Play Now, then save (S)" >&2
        exit 1
    }
    world="$(basename "$latest")"
fi
out="${2:-$root/r3d_compare}"

missing=()
for t in "$tileset_3d" "$tileset_2d"; do
    [ -f "$root/gfx/$t/tile_config.json" ] || missing+=( "$t" )
done
if [ "${#missing[@]}" -gt 0 ]; then
    echo "building tilesets: ${missing[*]}"
    "$root/tools/r3d_spike/compose_tilesets.sh" "${missing[@]}"
fi

args=( --world "$world" )
[ -n "${USERDIR:-}" ] && args+=( --userdir "$USERDIR/" )
rm -f "${out}_3d.png" "${out}_2d.png"
echo "loading world '$world'..."

cd "$root"
if CATA_R3D_CAMERA="${CAMERA:--45,30,20,1}" CATA_R3D_COMPARE="$out" \
    CATA_R3D_COMPARE_3D_TILESET="$tileset_3d" CATA_R3D_COMPARE_TILESET="$tileset_2d" \
    CATA_R3D_QUIT_AFTER_SHOT=1 ./cataclysm-tiles "${args[@]}"; then
    echo "saved ${out}_3d.png and ${out}_2d.png"
else
    echo "capture failed (see config/debug.log, lines starting 'r3d compare')" >&2
    exit 1
fi
if command -v magick > /dev/null; then
    magick "${out}_3d.png" "${out}_2d.png" +append "${out}.png" && echo "side by side: ${out}.png"
fi
