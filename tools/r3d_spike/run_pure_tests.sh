#!/usr/bin/env bash
# Builds and runs the r3d unit tests (tests/r3d_test.cpp) against the pure, SDL-free modules only,
# without linking the full cata_test binary. Needs tests/catch/catch.hpp (already in the repo).
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
printf '#pragma once\n#include "catch/catch.hpp"\n' > "$work/cata_catch.h"
printf '#define CATCH_CONFIG_MAIN\n#include "catch/catch.hpp"\n' > "$work/main.cpp"
# Quoted includes look next to the including file first, so the test is copied next to the stub
# cata_catch.h above instead of picking up the full one in tests/.
cp "$root/tests/r3d_test.cpp" "$work/r3d_test.cpp"
g++ -std=c++17 -O1 -I"$work" -I"$root/tests" -I"$root/src" -o "$work/r3d_tests" "$work/main.cpp" \
    "$work/r3d_test.cpp" "$root"/src/r3d_math.cpp "$root"/src/r3d_camera.cpp \
    "$root"/src/r3d_dda.cpp "$root"/src/r3d_hud_math.cpp "$root"/src/r3d_shapes.cpp "$root"/src/r3d_wall_rules.cpp
"$work/r3d_tests" "$@"
