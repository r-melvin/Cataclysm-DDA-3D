#!/usr/bin/env bash
# Saves the first-person 3D view of one turn facing each of the eight headings, without playing.
#
#   tools/r3d_spike/fp_shots.sh [world] [out_prefix]
#
# Loads the first character saved in [world] (default: the most recently played world) and,
# as soon as the map is on screen and before any input (so no turn passes), saves:
#   <out_prefix>_fp_n.png ... <out_prefix>_fp_nw.png  the whole window in first person facing
#                                                     N, NE, E, SE, S, SW, W, NW (HUD included)
#   <out_prefix>_orbit.png                            the orbit 3D view, for reference
#   <out_prefix>_map.txt                              player, both cameras (first person: heading,
#                                                     eye, yaw, pitch, fov), tiles and visibility
#   <out_prefix>_fp.png                               the eight headings in a labelled 4x2 grid
#                                                     (needs ImageMagick 6 or 7; skipped otherwise)
# then exits. Nothing in the options changes: the game switches to the 3D view and first person
# by itself (CATA_R3D_FP_SHOTS, see r3d::fp_shots_capture in src/r3d.cpp) and never writes the
# options file.
#
# Environment: TILESET_3D (default UltimateCataclysm; built into gfx/ with compose_tilesets.sh
# when missing), LOOK (level, up or down: the first-person tilt, default level), USERDIR (where
# save/ is; default this checkout, the build's default).
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
userdir="${USERDIR:-$root}"
tileset_3d="${TILESET_3D:-UltimateCataclysm}"
look="${LOOK:-level}"

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
out="${2:-$root/r3d_fp}"

if [ ! -f "$root/gfx/$tileset_3d/tile_config.json" ]; then
    echo "building tileset: $tileset_3d"
    "$root/tools/r3d_spike/compose_tilesets.sh" "$tileset_3d"
fi

headings=( n ne e se s sw w nw )
args=( --world "$world" )
[ -n "${USERDIR:-}" ] && args+=( --userdir "$USERDIR/" )
for h in "${headings[@]}"; do
    rm -f "${out}_fp_${h}.png"
done
rm -f "${out}_orbit.png" "${out}_map.txt" "${out}_fp.png"
echo "loading world '$world'..."

cd "$root"
if CATA_R3D_FP_SHOTS="$out" CATA_R3D_FP_TILESET="$tileset_3d" CATA_R3D_FP_LOOK="$look" \
    CATA_R3D_QUIT_AFTER_SHOT=1 ./cataclysm-tiles "${args[@]}"; then
    for h in "${headings[@]}"; do
        echo "saved ${out}_fp_${h}.png"
    done
    echo "saved ${out}_orbit.png"
    echo "saved ${out}_map.txt"
else
    echo "capture failed (see config/debug.log, lines starting 'r3d fp shots')" >&2
    exit 1
fi

im=""
if command -v magick > /dev/null; then
    im=magick
elif command -v convert > /dev/null; then
    im=convert # ImageMagick 6
fi
if [ -n "$im" ]; then
    labelled=()
    for h in "${headings[@]}"; do
        label="$(echo "$h" | tr '[:lower:]' '[:upper:]')"
        "$im" "${out}_fp_${h}.png" -gravity north -background black -fill white -pointsize 28 \
            -splice 0x40 -annotate +0+4 "$label" +repage "${out}_fp_${h}_labelled.png"
        labelled+=( "${out}_fp_${h}_labelled.png" )
    done
    # Two rows of four: N NE E SE / S SW W NW.
    "$im" \( "${labelled[@]:0:4}" +append \) \( "${labelled[@]:4:4}" +append \) -append +repage \
        "${out}_fp.png" && echo "contact sheet: ${out}_fp.png"
    rm -f "${labelled[@]}"
fi
