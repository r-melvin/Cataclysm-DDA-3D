#!/usr/bin/env bash
# Builds and runs shader_test.cpp against the compiled shaders in data/shaders (build the game
# first so the .spv files exist). Uses the SDL3 under SDL3_PREFIX (default /opt/sdl3, else the
# system one) and whatever Vulkan driver is available; on a headless machine run it under
# Xvfb with VK_ICD_FILENAMES pointing at lavapipe:
#   Xvfb :98 & DISPLAY=:98 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
#       tools/r3d_spike/run_shader_test.sh
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
prefix="${SDL3_PREFIX:-/opt/sdl3}"
out="${TMPDIR:-/tmp}/r3d_shader_test"
if [ -d "$prefix/include" ]; then
    g++ -std=c++17 -I"$prefix/include" -o "$out" "$root/tools/r3d_spike/shader_test.cpp" -L"$prefix/lib" -lSDL3
    export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    g++ -std=c++17 $(pkg-config --cflags sdl3) -o "$out" "$root/tools/r3d_spike/shader_test.cpp" $(pkg-config --libs sdl3)
fi
SDL_GPU_DRIVER="${SDL_GPU_DRIVER:-vulkan}" "$out" "$root/data/shaders"
