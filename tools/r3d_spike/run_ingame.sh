#!/usr/bin/env bash
# Runs the TILES build with the 3D renderer on a private virtual X display (Xvfb), in a throwaway
# user directory, starts a random "Play Now" character with xdotool and saves a screenshot of
# the first settled 3D frames. Nothing is drawn on, and no input is sent to, the real desktop:
# every X tool below is pinned to the virtual display and the script refuses to run otherwise.
#
#   tools/r3d_spike/run_ingame.sh [out.png]
#
# Environment: R3D_USERDIR (default ../r3d-userdir), R3D_DISPLAY (default :97),
# CATA_R3D_CAMERA="yaw,pitch,dist" for a fixed view, R3D_KEYS (default "n d"),
# R3D_STEPS="key key ..." sent after settling (one shot per key), VK_ICD_FILENAMES to pick a
# Vulkan driver.
set -u
root="$(cd "$(dirname "$0")/../.." && pwd)"
userdir="${R3D_USERDIR:-$root/../r3d-userdir}"
out="${1:-$userdir/shot.png}"
vdisplay="${R3D_DISPLAY:-:97}"
real_display="${DISPLAY:-}"
if [ "$vdisplay" = "$real_display" ] || [ "$vdisplay" = ":0" ]; then
    echo "refusing to use the real display $vdisplay" >&2
    exit 2
fi

mkdir -p "$userdir/config"
if [ ! -f "$userdir/config/options.json" ]; then
    echo '[ { "name": "RENDERER_3D", "value": "true" } ]' > "$userdir/config/options.json"
fi
rm -f "$out"

Xvfb "$vdisplay" -screen 0 1600x900x24 -nolisten tcp > "$userdir/xvfb.log" 2>&1 &
xvfb_pid=$!
cleanup() { [ -n "${game_pid:-}" ] && kill "$game_pid" 2>/dev/null; kill "$xvfb_pid" 2>/dev/null; }
trap cleanup EXIT
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb failed (see $userdir/xvfb.log)"; exit 1; }

# From here on only the virtual display exists as far as child processes are concerned.
export DISPLAY="$vdisplay"
unset WAYLAND_DISPLAY

cd "$root"
log="$userdir/config/debug.log"
log_base="$(wc -l < "$log" 2>/dev/null || echo 0)"
extra=()
env SDL_VIDEODRIVER=x11 CATA_R3D_CAMERA="${CATA_R3D_CAMERA:-}" \
    "${extra[@]}" \
    ./cataclysm-tiles --userdir "$userdir/" --noverify > "$userdir/stdout.log" 2>&1 &
game_pid=$!

# xdotool talks to $DISPLAY only, which is the virtual one.
for _ in $(seq 1 120); do
    win="$(xdotool search --pid "$game_pid" 2>/dev/null | tail -1)"
    [ -n "$win" ] && break
    kill -0 "$game_pid" 2>/dev/null || break
    sleep 1
done
[ -z "${win:-}" ] && { echo "no game window (see $userdir/stdout.log)"; exit 1; }
# Wait for the game's own "tileset loaded" line (the last step of loading) before the menus.
for _ in $(seq 1 ${LOAD_TIMEOUT:-300}); do
    tail -n +"$((log_base + 1))" "$log" 2>/dev/null | grep -q "Loaded tileset: Larwick Overmap" && break
    kill -0 "$game_pid" 2>/dev/null || break
    sleep 1
done
sleep "${MENU_WAIT:-6}"
for k in ${R3D_KEYS:-n d}; do
    xdotool key --window "$win" "$k"
    sleep "${KEYDELAY:-4}"
done

# Let the world settle, then take shots of the virtual display: one now, then one after each key
# of R3D_STEPS (e.g. R3D_STEPS="ctrl+Left ctrl+Up" for camera tests).
sleep "${SETTLE:-15}"
shot() { import -display "$vdisplay" -window root "$1" 2>/dev/null; }
base="${out%.png}"
shot "${base}-0.png"
n=1
for step in ${R3D_STEPS:-}; do
    xdotool key --window "$win" "$step"
    sleep "${STEP_WAIT:-4}"
    shot "${base}-$n.png"
    n=$((n + 1))
done
echo "screenshots: ${base}-*.png (virtual display only)"
