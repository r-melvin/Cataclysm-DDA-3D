# 3D renderer (r3d) status

Branch `feature/r3d-renderer`. Option `RENDERER_3D` (off by default). Plan: see the approved plan
(M0-M5). Test tools live in `tools/r3d_spike/`.

## Done and verified
- M0: custom SDL_GPU pass on the SDL_Renderer's device, wrapped as an SDL_Texture and composited
  into `w_terrain` (spike + in-game). Depth testing works.
- M1: chunked terrain/furniture meshes per submap, sprites sampled from tileset sheets, layered
  bg/fg sprites, per-submap `render_stamp()` dirty tracking.
- M2: orbit camera (ctrl+arrows, ctrl+PgUp/PgDn, ctrl+Home, ctrl+B), frustum culling, z-slices.
  Seen working in game on a rotated/tilted view (ASCII tileset only; no sprite tileset in repo).
- M3: per-vertex light from the visibility cache, recomputed each turn (not yet checked visually
  without CATA_R3D_FULLBRIGHT=1).
- Unit tests: `tools/r3d_spike/run_pure_tests.sh` (camera, DDA vs brute force, shapes): pass.

## Built but NOT verified in game
- M4 entities (`r3d_entities.cpp`): player/creatures/items did not appear in the last screenshot
  (only a stray glyph); needs debugging (check `all_creatures` loop, sprite lookup for ASCII
  tileset, billboard orientation).

## Not started
- M5: mouse picking. `r3d_dda.cpp` + `camera::screen_ray` exist and are tested but are not wired
  into `input_context::get_coordinates` (`sdltiles.cpp` ~7356). `tilecontext->overlay_strings`/
  `color_blocks` are skipped in 3D mode; the cursor box is not drawn.
- Per-position sprite variants and multitile (wall) subtile selection in the mesher.
- cata_test/CMake integration check for the new tests; non-TILES build check.

## Running in-game checks
`CATA_R3D_FULLBRIGHT=1 R3D_STEPS="ctrl+Right ctrl+Up" tools/r3d_spike/run_ingame.sh out.png`
runs on a private Xvfb display only (never the real desktop). Needs `Xvfb`, `xdotool`, `import`.
