#!/usr/bin/env bash
# Saves a side-by-side comparison of the same turn: the 3D view through an orthographic camera
# at the isometric angle, and the flat view drawn with an isometric tileset.
#
#   tools/r3d_spike/compare_iso.sh <world> [out_prefix]
#
# Loads the first character saved in <world>, captures <out_prefix>_3d.png and
# <out_prefix>_2d.png as soon as the 3D view has settled, writes <out_prefix>.png with both
# side by side (needs ImageMagick; skipped otherwise) and exits.
#
# Setup: the 3D shot uses the tileset chosen in the options (an orthogonal one such as
# UltimateCataclysm, with "3D renderer" on); the 2D shot uses COMPARE_TILESET (default
# Ultica_iso). Both must be installed, e.g. tools/r3d_spike/compose_tilesets.sh Ultica_iso.
# Environment: CAMERA (default -45,30,20,1 = yaw,pitch,distance,ortho), COMPARE_TILESET,
# USERDIR (passed as --userdir when set).
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
world="${1:?usage: compare_iso.sh <world> [out_prefix]}"
out="${2:-$root/r3d_compare}"
args=( --world "$world" )
[ -n "${USERDIR:-}" ] && args+=( --userdir "$USERDIR/" )
rm -f "${out}_3d.png" "${out}_2d.png"

cd "$root"
if CATA_R3D_CAMERA="${CAMERA:--45,30,20,1}" CATA_R3D_COMPARE="$out" \
    CATA_R3D_COMPARE_TILESET="${COMPARE_TILESET:-Ultica_iso}" CATA_R3D_QUIT_AFTER_SHOT=1 \
    ./cataclysm-tiles "${args[@]}"; then
    echo "saved ${out}_3d.png and ${out}_2d.png"
else
    echo "capture failed (see config/debug.log, lines starting 'r3d compare')" >&2
    exit 1
fi
if command -v magick > /dev/null; then
    magick "${out}_3d.png" "${out}_2d.png" +append "${out}.png" && echo "side by side: ${out}.png"
fi
