# First-person mode for the 3D renderer (r3d)

## Status

Branch `feature/r3d-first-person`. Switch it on with the option `R3D_CAMERA_MODE` = `first_person`
(graphics options, after "3D cutaway") or with **ctrl+F** (action `r3d_first_person`); `RENDERER_3D`
must be on. Orbit mode is unchanged when the option is `orbit`.

* FP0 camera: done (not yet seen in the game).
* FP1 billboards, items, markers: done (not yet seen in the game).
* FP2 controls: done (not yet seen in the game). Q/E turning was dropped by decision (arrows
  turn, `e` stays examine).
* FP3 ceilings and sky: done (not yet seen in the game).
* FP4 tweening and redraw loop: done (not yet seen in the game); instant by default.
* FP5 HUD: done in part (not yet seen in the game): compass strip with hostile ticks, edge alert,
  crosshair dot with label. Not done: look-level line, auto-travel caret.
* Minimap heading-up: done (not yet seen in the game). With `PIXEL_MINIMAP` on and option
  `R3D_FP_MINIMAP_ROTATE` (default on), the pixel minimap turns in first person so the way you
  face points up; your beacon stays put and the map turns under it. North-up in orbit mode, in
  the flat view and with an isometric tileset. Heading-up, the map is round: only the largest
  disc around you that fits the minimap is drawn, so turning never leaves empty corners (the
  plain minimap background is outside the disc at every heading). A small white arrow above your
  beacon points the way you face (always up).
* Heading saved with the game: done (not yet seen in the game). `uistate.r3d_fp_heading` is
  written whenever the first-person heading changes and read back on load, so you load facing
  the way you saved. Loading in orbit mode keeps the orbit camera; switching to first person
  then starts from the orbit camera's direction, as before.
* FP6 unseen tiles occlude: done in part (not yet seen in the game). Option
  `R3D_FP_OCCLUDE_UNSEEN` (default on): in first person never-seen places are drawn solid black
  and write depth (uniform `flags.x`, `block_uniforms` is now 144 bytes), so an unexplored wall
  hides the explored room behind it. Creatures, items and trees keep being left out when unseen.
  Not done from FP6: the linear-min sampler for distant floors, front-to-back chunk order, the
  item frustum pre-filter.
* Sound markers: done (not yet seen in the game). The markers the game queues for the turn
  (`sounds::get_footstep_markers`, what `cata_tiles::draw_footsteps_frame` draws in 2D) are
  posts on their tile in first person, using the tileset's `footstep`/`footstep_above`/
  `footstep_below` sprite (`highlight` when it has none). Since the game only places them where
  you cannot see, the posts are drawn in front of everything (light alpha 1 plus uniform
  `flags.y`), like the 2D marker drawn over the map. Sounds outside the horizontal field of
  view are yellow ticks on the compass strip instead (dimmer at the strip's ends when behind).
  The crosshair label now names only tiles you see or remember.
* Review of bearings and ceilings: done. The compass, the threat and sound ticks and the edge
  alert go through the pure `src/r3d_hud.{h,cpp}` (bearing = radians clockwise from north with
  the map's y south, the same convention as `camera::heading`, `fp_yaw()`, `eighths_towards` and
  `forward_eighths`), tested in `tests/r3d_test.cpp` for all eight headings. Found and fixed: a
  diagonal neighbour at the default 90 degree view sits exactly on the picture's edge, and
  float rounding lit the side bar for some headings and not others; the edge now counts as in
  the picture. Rear diagonals (135 degrees) light their side bar, only straight behind lights the
  bottom bar. Ceilings: windows and doors in a wall line used to get no ceiling (a hole above
  every door); the ceiling now runs on over them, and anything but open air above closes the
  room (a window pane, a shrub on a roof, solid furniture standing on open air). Trees still get
  none. Ceiling shade stays 170 (150 for the floor-sprite fallback): darker than any floor or
  wall cap (255), about the walls' darker sides (128-178), with the plaster colour as tint like
  plaster walls and caps, and lit by its own tile like the floor.
* FP7 z-levels: done in part (not yet seen in the game). With `R3D_FP_TWEEN_MS` above 0, taking
  stairs (a level up or down, landing on the same or a neighbouring tile) and climbing onto or
  off furniture ease the eye vertically like a step. Looking at another level in look mode
  (`<`/`>`, i.e. `view_offset.z`) tilts the view 45 degrees towards it (`camera::tilt_pitch`,
  display only) and shows "Looking: N level(s) up/down" under the compass. Not done: holes and
  stairwells were not checked, the eye on ramps uses the floor height.

What to test locally (FP0): toggle with ctrl+F in a house; the picture is a perspective view from
eye height; left/right arrows (with `R3D_WASD` on, the default) or ctrl+left/right turn by 45
degrees without spending game time; W/numpad 8 walk the way you face; middle-mouse drag glances
around and snaps back on release; ctrl+Home straightens the view (the heading is kept).
Options `R3D_FP_FOV` (60-110) and `R3D_FP_EYE_HEIGHT` (50-95).

FP1: creatures and trees stand straight up facing the screen with their feet on the floor (your
own sprite is not drawn); items on the floor are small upright stands; stairs and ladders also get
an upright copy of their sprite; the look/aim cursor is a box and the aim line is a row of posts.
Spawn a zombie next to you and another five tiles away to check they are not clipped or leaning.

FP2 keys: **Space** picks the tile you face in look mode, in aiming mode and in direction prompts
(close, examine, smash ...; option `R3D_FP_USE_HEADING`, on by default; it replaces the idea of
pre-selecting a default direction). **ctrl+Tab** turns (free) to the nearest visible hostile.
ctrl+PgUp/PgDn (the zoom keys) look one level up/down as a 45 degree tilt (display only; the mouse
wheel does the same).

FP3: a ceiling (plaster texture, or the floor sprite darkened when wall textures are off) closes
every room whose level above has a floor or wall; outdoors the sky colour follows the time of day.
Ceilings exist only in first-person meshes (`mesh_options.ceilings`).

FP4: `R3D_FP_TWEEN_MS` (0-250, default 0 = instant). When above 0 a step or turn slides into place
over that time; it is skipped while travelling, during activities and with ANIMATIONS off. The game
redraws at about 60 Hz only while an ease is live (`r3d::animating()`).

FP5 options: `R3D_FP_COMPASS` (off / heading / threats, default threats), `R3D_FP_EDGE_ALERT`,
`R3D_FP_CROSSHAIR`. The compass letters and the crosshair label are drawn in the game's own
font (the sidebar's), with a one-pixel shadow: `r3d::draw` leaves them in `r3d::hud_texts()` and
`sdltiles.cpp` draws them after the picture; the compass strip grows to fit the font's height.

Design for a first-person, **turn-based** view on top of the existing `r3d` renderer
(`src/r3d*.{h,cpp}`, SDL3 GPU). Target branch: `feature/r3d-first-person`. Companion to
`doc/R3D_STATUS.md`, which describes what the orbit renderer already does.

The model is Legend of Grimrock / Eye of the Beholder / Might & Magic X and the first-person mode
of roguelike front-ends such as NotEye: the world is seen from the character's eyes, the
character steps from tile to tile and turns in fixed steps, and nothing happens until the player
acts. It is explicitly **not** a real-time shooter and not Daggerfall-style free locomotion.

---

## 1. Vision and scope

### 1.1 Vision

* The game underneath is unchanged: same turns, same move costs, same 360° vision, same menus,
  same sidebar, same messages, same safe mode. The first-person (FP) mode replaces **only** the
  picture in `w_terrain` and the meaning of the movement/camera keys.
* The player stands in the middle of their tile at eye height, facing one of the eight map
  directions. "Forward" is the facing; W/A/S/D and the numpad walk relative to it, Q/E turn by
  45°. A step is one game move, a turn is free (no game time), as in Might & Magic X
  ([manual](https://mocagh.org/nwc/mm10-manual.pdf): "Rotating neither ends the party turn nor
  advances the in-game time").
* Everything the player can do by pointing at a tile today (look, examine, fire, throw, mouse
  click) still works, through the mouse ray that already exists (`r3d::pick`) and a new centre
  ray ("crosshair").
* The HUD makes up for the narrow view: compass strip with threat marks, the pixel minimap,
  sound/footstep marks as direction cues, and a one-key "turn to face it".

### 1.2 In scope

1. `R3D_CAMERA_MODE = orbit | first_person` with its own camera maths in `r3d::camera`.
2. 8-step turning, step/strafe movement mapped onto the existing `ACTION_MOVE_*` actions and
   `view_eighths()` rotation, optional short visual tween, never blocking input.
3. Turning off (or replacing) the orbit-only rendering tricks: billboard depth pull, cutaway,
   pan, orthographic projection, the "cut the roof away" level choice.
4. Ceilings and a sky background.
5. Upright (cylindrical) billboards for creatures, items and markers at eye level.
6. HUD overlay drawn with the normal 2D path on top of the 3D picture: compass, threat marks,
   crosshair, hovered-tile label; message log and sidebar stay where they are.
7. Look / examine / target through the centre ray; keyboard cursor and mouse still work.
8. Z-levels: look up/down, stairs/ladders/ramps, holes, multi-level buildings.
9. Vehicles: a first vehicle mesh (currently r3d draws no vehicles at all) and a camera that
   follows the vehicle's heading while driving.
10. Options and keybindings for all of the above.

### 1.3 Explicitly out of scope

* Any change to game rules: no vision cones, no facing-dependent combat, no backstab bonus, no
  turning cost, no real-time elements. (Open question 1 asks whether the user ever wants these;
  this design assumes not.)
* Free (analogue) mouse-look as the primary control. A "glance" (hold a button to look around,
  snaps back) is in scope; steering the character with the mouse is not.
* A new tileset or hand-made 3D models. FP reuses the tileset sprites, the furniture boxes and
  `data/raw/r3d/wall_sides.*`; new art is limited to a few atlas cells (ceiling, sky, generic
  item stand) added to `wall_sides.png`.
* Replacing the inventory, crafting, construction and other full-screen/overlay menus. They stay
  ImGui/curses overlays exactly as now.
* VR, gamepad analogue sticks, head tracking.
* Ambient sound positioning (the game has no audio positioning model; HUD cues only).
* Rewriting the lighting model (per-vertex light from the visibility cache stays).

---

## 2. Pitfalls found, and the decision that avoids each

Sources are listed in full in section 6. Each row: the pitfall, how it would bite this
codebase, the decision.

### Movement and turning

| # | Pitfall | Where it would bite here | Decision |
|---|---------|--------------------------|----------|
| M1 | **Inputs dropped during a move animation.** Grid games that block input until the step tween finishes feel sluggish and eat keypresses; the fix is to apply the logical move immediately and only animate a visual offset that decays towards zero ([pancelor, "Input buffering for grid games"](https://pancelor.bearblog.dev/input-buffering-for-grid-games/); [modd.io grid movement + input buffer](https://modd.io/forum/t/grid-based-movement-input-buffer/4533); the Unity "advanced grid movement" project adds a command queue for the same reason ([lutzgrosshennig](https://github.com/lutzgrosshennig/unity3d-advanced-grid-movement))). | Cataclysm's loop is `handle_input` → `handle_action` → redraw; any animation that waits would stall `input_context::handle_input` and break key repeat, auto-move and activities. | The game move happens first and completely, as today. The renderer keeps a purely visual `tween` (start eye position/yaw, end, start time) that decays over ≤ 120 ms; new input retargets it. Tween length 0 is supported and is the default while activities/auto-move run (§3.10). |
| M2 | **Grid steps feel "clunky when in a hurry"** ([bit-tech Grimrock review](https://bit-tech.net/reviews/gaming/pc/the-legend-of-grimrock-review/2/)); in open areas the 90° grid "feels rigid and frustratingly slow" ([GodisaGeek on MMX](https://godisageek.com/2014/02/magic-legacy-review/)). | Cataclysm is mostly outdoors and long walks are common. | Keep the game's own answers: auto-travel (`auto_travel_mode`, destination preview), `travel_to` via mouse click (the ray pick), and run mode. FP adds "8-direction" turning (45° steps, not 90°) so diagonal routes do not need zig-zags, and strafing keys so side-steps do not need a turn. |
| M3 | **Diagonal moves in a first-person grid.** Classic blobbers simply forbid them ("no diagonal movement at all", [Softpedia Grimrock diary](https://games.softpedia.com/blog/Legend-of-Grimrock-Diary-Walls-and-Soft-Modern-Gamers-268047.shtml)); MMX ranged attacks around corners confused players ([PC Gamer MMX review](https://www.pcgamer.com/might-and-magic-x-legacy-review)). | Cataclysm is natively 8-directional; forbidding diagonals would change the rules (out of scope) and make the keypad useless. | Heading has 8 values. Facing a diagonal and pressing W walks diagonally, which is a normal Cataclysm move. Diagonal strafes (numpad 7/9/1/3 and `MOVE_FORTH_LEFT` etc.) remain available and are rotated by `view_eighths()` exactly as in orbit mode (`action.cpp: get_delta_from_movement_action`). The crosshair tile at a diagonal heading is the diagonal neighbour, so examine/open/close act on it. |
| M4 | **Turning that costs time or is ambiguous between "turn" and "move".** | Cataclysm has no facing; a turn must not spend moves or trigger safe-mode re-checks or NPC reactions. | Turning is renderer-only state (`camera.heading`), handled in `handle_action` like `ACTION_R3D_ROTATE_*` today (no `u.moves` change, no turn advance). Rebinding Q/E in `r3d_key_action` is gated on the FP mode, like the existing WASD gate. |
| M5 | **Mouse-look in a turn-based grid game.** Grimrock lets the mouse look but not turn ([Softpedia](https://games.softpedia.com/blog/Legend-of-Grimrock-Diary-Walls-and-Soft-Modern-Gamers-268047.shtml)); MMX gives free-look on right-mouse hold that snaps back when you move ([Limbic Q&A via GameBanshee](https://gamebanshee.com/ka3u6)); Daggerfall Unity's mouse smoothing is disliked for its input lag ([dfworkshop forum](https://forums.dfworkshop.net/viewtopic.php?p=30251)). | The middle-drag orbit already exists (`sdltiles.cpp:6408`, `r3d::drag_orbit`). A free yaw would desynchronise `view_eighths()` (an `int`) from what the player sees. | Middle-drag becomes a **glance**: it offsets yaw/pitch for display only, never changes `heading`, and snaps back (tweened) on button release or on any action. No smoothing/acceleration on the mouse. Optional later: "glance commits heading" (round to nearest eighth on release) as an option, off by default. |

### Motion sickness and camera

| # | Pitfall | Where it would bite | Decision |
|---|---------|---------------------|----------|
| C1 | **Head bob and weapon sway** are the most reported nausea triggers; Xbox accessibility guideline 117 says to avoid repetitive camera motion and to allow disabling automatic camera movement ([Xbox XAG 117](https://devdocs.xbox.com/gaming/accessibility/xbox-accessibility-guidelines/117); [KCD forum](https://forum.kingdomcomerpg.com/t/head-bobbing-motion-sickness/39868)). | A step tween is "automatic camera movement". | No head bob, ever. Step/turn tween is linear-ish ease-out, short, and can be set to 0 (`R3D_FP_TWEEN_MS`). Vertical snaps when the surface height changes (stairs, ramps, climbing onto a counter) are eased over the same tween rather than jumping (as the Steady Sight mod does for ledges, [modrinth](https://modrinth.com/mod/steady-sight)). |
| C2 | **Smooth virtual turns are the worst offender**; discrete/snap turns reduce cybersickness (Kelly et al., N=201, [NSF PAR](https://par.nsf.gov/servlets/purl/10591601); Farmani & Teather 2018 report ~40 % lower SSQ with viewpoint snapping, [GI 2018](https://www.csit.carleton.ca/~rteather/pdfs/GI_2018_viewpoint_snapping.pdf)). Snap turns can disorient ([arXiv 2502.15227](https://www.arxiv.org/pdf/2502.15227)). | A 45° snap with no cue makes it easy to lose track of which way is north. | Turns are snaps of 45° (optionally tweened over ≤ 120 ms, which is still perceived as a snap). The compass strip (§3.7) always shows the heading and the cardinal letters, so orientation is readable without memory. Combining several mitigations has no extra benefit (Kelly et al.), so no vignette by default; `R3D_FP_VIGNETTE` is an optional cheap edge darkening. |
| C3 | **Field of view.** Too narrow feels claustrophobic and sick-making; too wide distorts sprites and lets things "scoot" at the frustum edge ([No Man's Sky thread](https://steamcommunity.com/app/275850/discussions/0/1762481957310661227)); the guideline is an adjustable FOV ([XAG 117](https://devdocs.xbox.com/gaming/accessibility/xbox-accessibility-guidelines/117)). | Billboards at a wide FOV near the screen edge get stretched; `w_terrain` is often very wide (sidebar layouts). | `R3D_FP_FOV` is the **horizontal** FOV, 60–110°, default 90°; vertical FOV is derived from the viewport aspect (`fovy = 2·atan(tan(fovx/2)/aspect)`), so wide terrain windows do not become fisheye vertically. |
| C4 | **Near-plane clipping into walls** and loss of depth precision when the near plane is pushed in ([Unreal answers](https://answers.unrealengine.com/questions/5074/view.html); [Khronos](https://community.khronos.org/t/opengl-camera-glitch/106162); [bugnet.io](https://bugnet.io/blog/how-to-fix-near-plane-clipping-into-geometry-first-person)). | `r3d_camera.cpp: near_plane = 0.5f`. The eye is in the centre of a tile, so an adjacent wall face is exactly 0.5 tiles away and would be clipped by the near plane; a creature on the adjacent tile is 1.0 away with a 0.5-wide sprite, also clipped. | FP uses `near = 0.05` and `far = 200` (the reality bubble is 132 tiles across; nothing further is loaded). The device already prefers a 32-bit float depth buffer (`r3d_gpu.cpp:94`), which keeps z-fighting away at these values. If fighting shows up on coplanar slabs, switch to reversed-Z (compare op `GREATER`, swap near/far in `perspective`) before touching the near plane again. |

### Awareness

| # | Pitfall | Where it would bite | Decision |
|---|---------|---------------------|----------|
| A1 | **"First-person loses the strategic overview roguelikes depend on"** ([itch.io discussion on NotEye-style FPP](https://itch.io/post/6884902)); monsters reaching the party from behind target the back row in Eye of the Beholder ([MobyGames](https://www.mobygames.com/game/835/)). | Cataclysm's player sees 360° and the game tells them about every visible hostile (safe mode, sidebar monster list). In FP the picture shows ~90° of that. | Rules unchanged: the player still *sees* behind them. The HUD shows it: compass strip with a tick per visible hostile (colour by attitude, bigger when adjacent), a red edge flash when a hostile is adjacent and off-screen, and `r3d_fp_face_threat` turns the heading towards the nearest visible hostile. Safe mode continues to stop movement. |
| A2 | **Sound cues** (footsteps `?` markers, "You hear a ...") are placed on map tiles that may be off-screen. | `sounds.cpp: sound_markers`, `game::draw_footsteps`, `cata_tiles::draw_footsteps_frame` draw them into the 2D terrain window, which FP replaces. | Sound markers become (a) upright billboards on their tile when in the frustum and (b) ticks on the compass strip when not, for the turn they are shown. |
| A3 | **Getting lost / which way is north.** Grimrock ships an automap by default and only removes it in "Oldschool Mode" ([Steam](https://store.steampowered.com/app/207170)); Daggerfall relies on a compass. | Cataclysm's map (`m`) and overmap are full-screen overlays that break the FP flow for a quick check. | The existing pixel minimap (`w_pixel_minimap`, `PIXEL_MINIMAP` option, drawn by `tilecontext` independently of r3d) is recommended on in FP; the compass strip carries N/E/S/W. Opening `m`/overmap stays as today. |

### Fit with Cataclysm's mechanics

| # | Pitfall | Where it would bite | Decision |
|---|---------|---------------------|----------|
| F1 | **"Does the player see what the game says they see?"** A vision cone would be a rules change; a 360° game with a 90° picture means things attack from where nothing is drawn. | `you.sees()` gates every billboard (`r3d_entities.cpp`). | Keep 360° vision. The FP picture is a *window* on the game's visibility, not a replacement; the HUD carries the rest (A1). Document this prominently in the option text. |
| F2 | **Look mode moves the view centre.** `look_around` sets `u.view_offset`, so `g->ter_view_p` (the `center` passed to `r3d::draw`) wanders away from the player; `draw_ter` builds the visibility cache for `ter_view_p.z()`. | In FP the camera must not teleport to the cursor. | In FP mode `r3d::draw` uses `get_avatar().pos_bub()` for the eye and only uses `center` for (a) the z-level whose cache is current and (b) nothing else; the look cursor is drawn as a marker box where it is. `view_offset.xy()` is ignored by the camera; `view_offset.z()` becomes "look up/down one level". |
| F3 | **Targeting and examining.** `choose_adjacent` and `target_handler` expect a direction key or a mouse tile. The orbit markers (`animation_line`, `cursor`) are flat quads that vanish at eye level. | `r3d.cpp:918` builds markers from `tilecontext->get_frame_markers()`; `emit_flat` for lines. | Centre ray: `r3d::crosshair()` = `pick()` at the viewport centre, cached per frame. New action `r3d_fp_select_ahead` in look/target contexts moves the cursor to the crosshair tile. Markers become upright: cursor keeps its box (already `box = true`), line points become short posts, the line end a taller post. `choose_adjacent` pre-selects the heading tile when a `r3d_fp_use_heading` option is on (§3.8). |
| F4 | **Large menus.** Inventory/crafting are ImGui or curses overlays. | Nothing changes: they draw over `w_terrain` as now; `r3d::draw` is only called for `w_terrain` (`sdltiles.cpp:4118`). | Keep. Ensure the FP HUD is drawn *before* overlays (it is part of the `w_terrain` draw, §3.7). |
| F5 | **Vehicles are not drawn by r3d at all** (no `vehicle`/`vpart` reference in `src/r3d*`); driving in FP would mean an invisible car. The view-ahead offset (`game::set_driving_view_offset`) moves the 2D view, and vehicle headings are in degrees, not eighths. | `handle_action.cpp:2355` already drives with `iso_rotate::no`, so steering is unaffected by `view_eighths()`. | Milestone 8 adds a vehicle mesh (boxes per part, rotated by `veh->face`). While `has_vehicle_control`, the FP yaw follows the vehicle's facing continuously (not snapped) and the eye sits over the driver's seat; `driving_view_offset` is ignored by the camera. Until that milestone lands, FP shows a warning marker and the option text says vehicles are not yet drawn. |
| F6 | **Z-levels, stairs, ladders, holes.** The orbit view cuts the roof away (`open_sky` test at `r3d.cpp:821`) and stretches levels (`z_scale`). From inside a room you would see no ceiling and the black clear colour. | `visible_faces()` never emits bottom faces (`r3d_shapes.cpp:114`: "Bottom faces never are"); under a roof `z_hi = view_z`, so the level above is not even meshed. | FP always meshes `z+1` (and above) and emits **ceiling faces** (§3.5). Stairs/ladders keep their sprite on the floor but get an upright billboard copy so they are visible from eye height; `>`/`<` are unchanged. Looking up/down a level (`view_offset.z`) tilts the camera. |
| F7 | **Items on the floor, many creatures per tile, tiny sprites.** Flat item quads (`emit_flat`) foreshorten to a line at eye level; distant 1-tile sprites become a few pixels. | `item_size = 0.6`, flat at `surface + 0.02`. | Items become small upright billboards (0.45 tile) standing on the surface, cylindrical; stacks show the top item plus a small count badge on the HUD label when the crosshair is on them. Cataclysm allows one creature per tile, so "many creatures" is a non-issue; mounted/ridden pairs are drawn as the game does (rider layer). Tiny monsters are tiny in the tileset already; the 2× supersample (`CATA_R3D_SUPERSAMPLE`) keeps them from shimmering. |
| F8 | **Remembered vs seen, night and darkness.** The orbit view sepia-tints memory and discards never-seen tiles (`light_sampler`, `r3d_block.frag`). In FP a dark corridor is a black screen, and discarded unseen walls let you "see through" into seen rooms behind them. | `discard` on near-black light in the fragment shader. | Keep the sepia memory (it reads well as "remembered"). For never-seen tiles in FP: instead of `discard`, draw them as **black but depth-writing** (`R3D_FP_OCCLUDE_UNSEEN`, default on), so an unseen wall still hides what is behind it, matching what a person would see (nothing). At night the picture goes dark exactly as the 2D view does; the HUD stays readable because it is 2D. |
| F9 | **Long activities and auto-move** redraw many times per second; a tween per step would turn auto-travel into a slideshow, and the `ANIMATION_DELAY` loop in the game already paces animations. | `u.is_auto_moving()`, `activity` running. | Tweens are skipped (instant) while `u.is_auto_moving()` or `u.activity` is set; turning while auto-moving is allowed but does not change the route (routes are stored as map steps; `get_movement_action_from_delta` already inverts `view_eighths()` so stored routes survive heading changes). |

### Art

| # | Pitfall | Where it would bite | Decision |
|---|---------|---------------------|----------|
| R1 | **Spherical (camera-facing) billboards lean** when seen from above/below; cylindrical billboards keep trees upright ([SIGGRAPH '98 advanced OpenGL notes, billboards](https://www.opengl.org/archives/resources/code/samples/advanced/advanced98/notes/node42.html); [Lighthouse3D billboarding](https://www.lighthouse3d.com/opengl/billboarding)). | `emit_billboard` builds a quad along `right`/`up` from the orbit camera and pulls its depth (`max_billboard_pull`, `pull.w` uniform) so the leaning top does not sink into walls. At eye height the lean would put sprites' heads into the ceiling and the pull would push adjacent creatures through the near plane. | In FP: `up = (0,0,1/z_scale)`, `right = ground_right()`, `pull.w = 0`. No depth pull, no lean. Entities adjacent to the eye are clamped to stand at ≥ 0.35 tiles from the eye (sprites that would cross the near plane are slid back along the ray, not scaled). |
| R2 | **3/4-view tileset sprites at eye level.** Sprites are drawn from a ~30° elevated viewpoint; at eye level they still read (Doom used the same trick) but floor sprites, furniture lids and walls' top faces do not. | Floors are top faces only (fine, seen at a grazing angle); furniture boxes already use the lid rows for the top and the side rows for the sides (`furniture_box_layout`), which is what an eye-level view sees. | Keep boxes. Lower the default `R3D_LEVEL_HEIGHT` consideration: at eye height the texture of a 1-tile wall is stretched over 1.8 tiles vertically; the wall-side atlas (`wall_sides.png`) is already made for that. Floors seen at grazing angles need **linear or mipmapped** sampling at distance: FP creates a second sampler (linear min filter, nearest mag) and uses it for terrain batches; sprites keep nearest. |
| R3 | **Walls very close to the camera**: a 32 px texture over a full-screen wall. | NEAREST sampler (`r3d_gpu.cpp:175`), no mipmaps. | Accept the pixel look for tileset sprites (it is the game's style) but make the wall-side atlas cells 64–128 px where possible (`make_wall_textures.py`), and bias the texel density by keeping `wall_sides` for walls (option already on by default). |
| R4 | **Ceilings and roofs.** No bottom faces exist; buildings' roofs are terrain on `z+1`. | `visible_faces`, `open_sky` heuristic. | §3.5: ceilings are emitted by the chunk *below* as a face at `z = 1.0` of a cell whose level above has a floor/roof, lit by the tile below (so the existing `light_ref` grid, which only covers `dz ∈ {0,1}`, works unchanged). Outdoors: a sky gradient by time of day replaces the dark clear colour. |

### Performance

| # | Pitfall | Where it would bite | Decision |
|---|---------|---------------------|----------|
| P1 | **Full-window perspective with long sightlines** (a highway at ground level) draws far more chunks than a 45° orbit view, and `cull_mode = NONE` doubles the fragment work on back faces. | `r3d.cpp:869` loops all `MAPSIZE²` submaps per level and frustum-tests each; the sky-high far plane (400) accepts everything. | Far plane 200; draw front-to-back (sort `visible` by chunk distance so early-Z rejects overdraw); unseen tiles become depth-writing black (F8) which also occludes. Back-face culling is a later milestone because quad winding is not yet consistent (verify with a debug colour pass first). Measure before optimising: `CATA_R3D_SUPERSAMPLE=1` halves fill cost if needed. |
| P2 | **Entity scan radius.** `creature_radius = 45`, `item_radius = 20` scan a square around the centre each frame (the item loop is 41×41×levels map lookups). | Same per frame cost as orbit, but FP wants a tween loop at 60 Hz. | Entities are rebuilt only when the game state changed (turn, visibility generation, or creature moved) and otherwise re-uploaded as-is; the tween only changes the view matrix. Items are additionally frustum-culled by tile before the map lookup. |

### Prior art, summarised

* **Grimrock / Dungeon Master / EoB** ("blobbers"): the discrete grid survived the move to real time because lockstep movement and square-by-square puzzles work ([Digital Antiquarian](https://filfre.net/?p=3803)). Lesson for us: keep the grid honest (the eye is always at a tile centre), make turning cheap, give an automap.
* **Might & Magic X Legacy**: tile-based WASD + QE, right-mouse free-look that snaps back, turning is free; players disliked the 90° limit and corner-cases for ranged attacks ([GameBanshee Q&A](https://gamebanshee.com/ka3u6), [PC Gamer](https://www.pcgamer.com/might-and-magic-x-legacy-review), [PCGH forum](https://extreme.pcgameshardware.de/threads/might-magic-x-legacy-finaler-release-im-januar.311590/)). Lesson: 45° steps and a crosshair that is an actual tile fix the two complaints.
* **Daggerfall Unity**: moved to smooth first-person controls and treats mouse smoothing as an opt-in with known input lag ([dfworkshop 0.10.23](https://www.dfworkshop.net/daggerfall-unity-0-10-23/), [forum](https://forums.dfworkshop.net/viewtopic.php?p=30251)). Lesson: keyboard turning must exist and must not be filtered.
* **NotEye (Necklace of the Eye)**: a front-end for ADOM/Hydra Slayer/others with a hidden First Person Perspective mode on top of an unchanged roguelike ([roguetemple](https://www.roguetemple.com/z/noteye)). It is the closest prior art to this design (renderer-only FP over a turn-based grid game) and the fact it stayed "hidden" is a warning: without HUD support for the off-screen world the mode is a curiosity. The itch.io criticism (loss of overview) is the one we answer with A1–A3.
* **ROGUE-FP** keeps classic rules under a first-person camera but switched to a time-based system and free movement ([Steam](https://store.steampowered.com/app/2343160/ROGUEFP/), [r/roguelikes](https://www.reddit.com/r/roguelikes/comments/1ostn75/roguefp_version_37_first_person_traditional)). We do not: the user wants turns untouched.
* **Falcon's Eye / Vulture**: isometric mouse-driven NetHack front-ends ([Wikipedia](https://en.wikipedia.org/wiki/Falcon%27s_Eye), [nethackwiki](https://nethackwiki.com/wiki/Vulture)); not first person, but they show the recurring cost of a graphical front-end: readability of the rotated map and awkward mouse control ("customizable keyboard commands for those who find mouse controls awkward", [LinuxLinks](https://www.linuxlinks.com/FalconsEye/)). Lesson: keyboard-first.
* **Cataclysm's own history**: the upstream tracker could not be searched from this session (GitHub search API is blocked here); the isometric tilesets (Ultica_iso) and this branch's `CATA_R3D_COMPARE` tooling are the in-repo precedent. The orbit renderer's cutaway and remembered-tile sepia are the solutions it already found for walls hiding the player and for memory; FP keeps the second and drops the first.

---

## 3. Detailed design

Conventions: "stretched space" means world space with `z` multiplied by `camera::z_scale`
(see `r3d_camera.h`). Tile space has `y` south; world space flips it (`tile_to_world`).

### 3.1 Camera mode

**`r3d_camera.h`** — extend `struct camera`:

```cpp
enum class camera_mode : uint8_t { orbit, first_person };
camera_mode mode = camera_mode::orbit;

// first person only
int   heading = 0;      // 1/8 turns clockwise from north, 0..7 (map convention, y south)
float look_pitch = 0.f; // radians, +up; clamped to [-1.2, 1.2] (~70°)
float eye_height = 0.8f;// tile widths above the standing surface, in stretched space
float glance_yaw = 0.f; // mouse glance offsets, display only (snap back to 0)
float glance_pitch = 0.f;
float fovx = 1.5708f;   // 90° horizontal; fovy derived per aspect in projection()
```

* `eye()`: in FP returns `stretched_target() + (0,0,eye_height)` where `target` is the player's
  tile centre at the standing surface (`surface_height()` of the tile, so standing on a counter
  raises the eye). `stretched_target()` is unchanged.
* `view()`: in FP, `look_at(eye, eye + forward, up)` with
  `forward = (sin(yaw_w)·cos(p), cos(yaw_w)·cos(p), sin(p))`, `yaw_w = heading·π/4 + glance_yaw`
  (world frame: heading 0 = +y north, 2 = +x east), `p = look_pitch + glance_pitch`. The
  `stretch` matrix is still multiplied in, exactly as for orbit, so geometry proportions do not
  change between modes.
* `projection(aspect)`: FP never uses `orthographic`; uses `fovy = 2·atan(tan(fovx/2)/aspect)`,
  `near = 0.05`, `far = 200` (constants `fp_near_plane`, `fp_far_plane` next to the existing
  ones in `r3d_camera.cpp`).
* `forward_eighths()`: in FP returns `heading` directly (no `atan2` rounding). This is the one
  function `input_context::get_direction` and `action.cpp` rely on, so all direction rotation
  keeps working with no change outside the renderer.
* `ground_right()`: `(cos(yaw_w), sin(yaw_w), 0)` where the world yaw is derived from heading.
* `screen_ray()` is unchanged (works from the matrices); `view_frustum()` likewise.
* `rotate_steps(n)`: in FP, `heading = (heading + n) mod 8` and starts a yaw tween.
  `pitch_steps(n)`: in FP, nudges `look_pitch` by 15°. `zoom_steps()`: in FP, no-op (or adjusts
  `fovx` by ±5° within the option range; open question 5).

**Why the stretch stays.** `R3D_LEVEL_HEIGHT` (default 180 %) is what makes a storey taller than a
tile is wide. Removing it in FP would put a 1-tile-tall human sprite exactly as tall as a wall
and the eye (0.8) just under the ceiling. The stretch is therefore kept as a *world* property;
what FP turns off is every place the orbit code uses it as a trick (billboard lean/pull shrink,
target stretching, `pitch`/`distance`). Eye height 0.8 tile with a 1.8-tile storey puts the
camera at roughly the eye level of a 1-tile sprite and 1.0 tile below the ceiling.

**State** (`r3d.cpp: struct state`): add `fp_tween` (see §3.10), `crosshair` (cached
`voxel_hit` + `tripoint_bub_ms` for the current frame), `fp_prev_eye`/`fp_prev_yaw` for the
tween, and `fp_heading_persist` saved in `uistate` so the heading survives a save/load (open
question 4).

**Mode switching**: `R3D_CAMERA_MODE` option (orbit/first_person) read at the top of
`r3d::draw` like `R3D_LEVEL_HEIGHT` is today. `r3d::toggle_first_person()` flips it and is bound
to a new action `r3d_first_person` (default ctrl+f). `r3d::reset()` in FP resets pitch, glance
and FOV but keeps the heading.

### 3.2 Controls and keybindings

All new actions are registered in `game::get_player_input`/`handle_action` next to the existing
`r3d_*` list (`game.cpp:2702`) and dispatched in `handle_action.cpp:3150ff`. New `action_id`s go
after `ACTION_R3D_RESET` in `action.h`, names in `action.cpp:353ff`, defaults in
`data/raw/keybindings.json` after `r3d_reset`.

| Action id | Default | FP behaviour | Mapping |
|-----------|---------|--------------|---------|
| `MOVE_FORTH` / `MOVE_BACK` / `MOVE_LEFT` / `MOVE_RIGHT` and diagonals | numpad, vi-keys, W/S/A/D with `R3D_WASD` | Step relative to heading: forth = heading, back = heading+4, left/right = strafe (heading∓2), diagonals = heading±1 / heading±3. | Unchanged code path: `get_delta_from_movement_action(act, iso_rotate::yes)` rotates by `view_eighths()` = `heading`. |
| `r3d_rotate_left` / `r3d_rotate_right` | ctrl+←/→; plain ←/→ when `R3D_WASD` is on (already so in orbit mode, `r3d_key_action`); optionally **Q / E** with `R3D_FP_QE_TURN` | Turn heading by one eighth. No game time. | `r3d::rotate(±1)` → `camera::rotate_steps`. `r3d_key_action` (`input_context.cpp:478`) gains `'q'`/`'e'` → rotate when `mode == first_person && R3D_FP_QE_TURN`. In the default keymap (`data/raw/keybindings.json`, `DEFAULTMODE`) `q` is **unbound** but `e` is **examine** (and `E` is eat, not examine), so `R3D_FP_QE_TURN` defaults to **off**; with it on, examine is reached through the action menu or a rebind. Arrow-key turning is the primary binding (open question 2). |
| `r3d_pitch_up` / `r3d_pitch_down` | ctrl+↑/↓; plain ↑/↓ with `R3D_WASD` | Look up/down 15° (display only). | `r3d::pitch(±1)`. |
| `r3d_fp_look_level_up` / `_down` | ctrl+PgUp/PgDn (replaces `r3d_zoom_*` in FP) | Sets `u.view_offset.z()` ±1 and tilts `look_pitch` to ±45°, so "look at the floor above" is one key. | Same thing `look_around`'s `change_lv` does; `r3d_zoom_*` keep their meaning in orbit. |
| `r3d_fp_select_ahead` | **Space** in the `LOOK` and `TARGET` contexts only (Space is unbound in both in the default keymap; not added to DEFAULTMODE) | Move the look/target cursor to the crosshair tile. | New action registered in `game::look_around` and `target_ui` contexts; they already handle `SELECT`/mouse, so this sets `lp`/`dst` and continues the loop. `TARGET` already has `*` = `TOGGLE_SNAP_TO_TARGET`, which stays. |
| `r3d_fp_face_threat` | **ctrl+Tab** (DEFAULTMODE). Plain Tab is `autoattack`, which already attacks the nearest hostile and so complements this. | Turn to face the nearest visible hostile (or the newest sound marker when none). | Picks from `g->all_creatures()` with `you.sees()` and `attitude_to(you) == Attitude::HOSTILE`, computes `direction_from` → eighths, sets heading with a tween. |
| `r3d_first_person` | **ctrl+F** | Toggle orbit ↔ first person. | New `r3d::toggle_first_person()`. |
| `r3d_reset` | ctrl+Home | FP: reset pitch/glance/FOV; keep heading. | existing |
| Mouse: left click | — | Unchanged: `get_coordinates` → `r3d::pick` → travel / select / fire. | `sdltiles.cpp:7427` |
| Mouse: middle drag | — | Glance (display-only yaw/pitch offsets, snaps back on release). Shift+middle no longer pans. | `r3d::drag_orbit` becomes `r3d::drag_look` with a mode branch; `drag_pan` is a no-op in FP. |
| Mouse wheel | `r3d_zoom_*` | FP: no-op by default; option `R3D_FP_WHEEL = none | fov | look_level`. | `game::zoom_in/out` (`game.cpp:6396`) already branch on `r3d::active()`. |

Input contexts: `DEFAULTMODE` gets the new actions; `LOOK` and `TARGET` (ranged.cpp's
`target_ui`) get `r3d_fp_select_ahead`; nothing else changes. `r3d_key_action` keeps its
"DEFAULTMODE only, no modifiers, single key" gate so menus never see remapped keys.

Driving (`has_vehicle_control`): movement keys reach `pldrive` with `iso_rotate::no`
(`handle_action.cpp:2355`), i.e. ← → steer and ↑ ↓ accelerate regardless of heading; unchanged.
Q/E turning is ignored while driving if `R3D_FP_DRIVE_FOLLOWS_VEHICLE` is on (§3.9).

### 3.3 Turning off the orbit-only tricks

| Trick | Where | FP behaviour |
|-------|-------|--------------|
| `z_scale` as a camera property (`stretched_target`, `eye()` uses pitch/distance) | `r3d_camera.cpp:34-50` | Stretch kept in `view()`; FP `eye()` ignores `pitch`/`distance`. `R3D_LEVEL_HEIGHT` still applies. |
| Billboard lean + depth pull | `r3d_entities.cpp: emit_billboard` (`lean`, `pull`, `top_alpha`), `r3d.cpp:972-976` (`uniforms.pull`), `r3d_block.vert` (`u.pull.w > 0`) | `entity_view` gains `bool upright_billboards`. When set, `emit_billboard` uses `up = (0,0,1)` (divided by `z_scale` as now), `right = cam.ground_right()`, `top_alpha = 0`; `draw()` sets `uniforms.pull[3] = 0`. No shader change needed. |
| Cutaway | `set_cutaway()` (`r3d.cpp:429`), `R3D_CUTAWAY` | `set_cutaway` returns early when `mode == first_person` (the player is the camera; nothing can stand between). |
| Pan (`s.pan`, shift+middle drag) | `r3d.cpp:656`, `draw()` line 852 | Ignored in FP (`target` = player tile). `reset()` clears it. |
| Orthographic projection, `CATA_R3D_CAMERA` env | `camera::projection`, `apply_env_camera` | Ortho forced off in FP. `CATA_R3D_CAMERA` gains a 5th field `fp` (heading, pitch) for screenshot tooling. |
| Roof removal (`open_sky` → `z_hi = view_z`) | `r3d.cpp:821-826` | FP: `z_hi = min(view_z + max_levels_above, OVERMAP_HEIGHT)` always (ceilings need `z+1` meshed; frustum culling removes most of it). |
| View depth below (`R3D_VIEW_DEPTH`) | `r3d.cpp:817` | Kept (holes, balconies, looking down stairs). |
| Player billboard | `r3d_entities.cpp:214` (`critter.is_avatar()` always drawn) | Skipped in FP (you do not see your own sprite); drawn again when `view_offset.z != 0` or glance pitch < −60° is *not* needed; keep it simple: never. |
| Mouse drag orbit (`drag_orbit`) | `r3d.cpp:646` | Becomes glance (§3.2). |
| Markers as flat quads | `r3d_entities.cpp:237` | FP: upright posts (§3.6). |

### 3.4 Level choice, visibility and "unseen occludes"

* `z_lo = view_z − R3D_VIEW_DEPTH`, `z_hi = view_z + 5` (FP) where `view_z` is the player's level
  (`center.z()` may differ while looking up/down; the light sampler's `vision_top` stays
  `center.z()` because that is the level the game computed a visibility cache for, see
  `game::draw_ter` → `here.update_visibility_cache(ter_view_p.z())`).
* `light_sampler::color_at` is unchanged. A new uniform flag (`cut2.z`, currently unused) tells
  `r3d_block.frag` to **not** discard black-lit fragments but write `out_color = 0` with depth,
  when `R3D_FP_OCCLUDE_UNSEEN` is on. Entity billboards keep discarding (an unseen creature is
  not drawn at all; they are already filtered by `you.sees`).
* Sky: `begin_pass` clear colour becomes a function of `calendar`/weather brightness (daylight
  blue-grey → night near-black); a gradient quad at the far plane is a later nicety.

### 3.5 Ceilings

Implemented in the mesher (`r3d_mesh.cpp: build_chunk_mesh`) and shape rules
(`r3d_shapes.cpp`):

* New `face_bit face_ceiling = 32`. `visible_faces(self, n)` sets it for `floor`, `block`
  and `prism` cells when `n.up.kind == shape_kind::floor` (the level above has a floor/roof) and
  `self.kind != shape_kind::block` (a full wall needs no ceiling inside it). For a wall cell the
  wall's own top face already closes the column.
* Geometry: a quad at `z = 1.0` of the cell (cell-local, before the stretch), wound to face
  down. Texture: `tile_info_cache::ceiling(tiles)` — a new `wall_sides.json` entry
  `"ceiling"` (plain plaster/concrete cell in `wall_sides.png`, generated by
  `make_wall_textures.py`), tinted by the `cap_tint` of the nearest wall in the chunk if any,
  else neutral. Fallback when wall textures are off: the floor sprite of the tile above,
  darkened (`shade = 150`).
* Light: `light_ref{lx, ly, dx=0, dy=0, dz=0, shade=170}` — the tile *below* the ceiling, which
  is the cell's own tile, so `update_chunk_light`'s 2-level grid needs no change.
* `chunk_stamp` already combines neighbours whose shapes affect faces; confirm it includes the
  submap above (`neighbourhood.up` is used, so it must), otherwise add it.
* Outdoor tiles (`n.up.kind == empty`): no ceiling; the sky shows. Overhanging roofs over porches
  are handled automatically (the tile above has a floor).
* Orbit mode: ceiling faces exist in the mesh but are back-facing/invisible from above when the
  roof is drawn and the roof is cut away when under it; to avoid them darkening orbit views when
  looking up from below a hole, emit them only when the chunk is meshed with
  `mesh_options.ceilings = true` (FP). Switching modes rebuilds chunks (`release_all_chunks`),
  the same way `R3D_WALL_TEXTURES` does.

### 3.6 Billboards, items and markers at eye level

In `r3d_entities.cpp`, driven by `entity_view::upright_billboards` (FP) and
`entity_view::eye` (world position of the camera):

* **Creatures**: `emit_billboard` with vertical `up`, no pull (§3.3). Mirroring follows
  `cata_tiles::character_layers::mirrored` as now. Feet at `surface(p)`. If the sprite's quad
  would come within `0.35` of the eye (adjacent creature at a diagonal corner), slide it away
  from the eye along the ground until it is not (depth order with the wall behind still holds
  because the tile is 1.0 away).
* **Items**: `emit_item_stand`: an upright billboard 0.45 tiles tall (the sprite's own aspect),
  centred on the tile, standing on `surface(p) + 0.02`; only the top item of the stack, as now.
  Items on furniture tops (counters) stand on the prism top via `surface()`. The `item_radius`
  square loop is pre-filtered by `frustum.intersects` of the tile box.
* **Uprights** (trees, lockers): same cylindrical path. Lockers that back onto a wall keep their
  back-offset from the mesher (`z_base`/position unchanged).
* **Markers**: `cursor` keeps `emit_marker_box` (a 1-tile-high box outline reads very well in
  FP); `animation_line` points become 0.3-wide, 0.6-tall posts (`emit_marker_box` with a smaller
  footprint); `line_end`/`highlight` a full-height box in their own sprite. Markers still ignore
  light (white).
* **Sound/footstep markers**: new `entity_view::marker` entries with `sprite = "footstep"` (the
  tileset's) drawn as posts; fed from a new accessor `sounds::marker_positions()` (today
  `sound_markers` is file-static in `sounds.cpp`; expose a const view).
* **Stairs/ladders/ramps**: terrain with `TFLAG_GOES_DOWN/UP` additionally gets an upright copy
  of its sprite (`uprights` list), so a staircase is visible at eye level, not just as a floor
  decal.

### 3.7 HUD

Drawn by the 2D path after `r3d::draw` returned `true` (in `sdltiles.cpp` right after the
`SDL_RenderTexture` composite, or inside `r3d::draw` via the `SDL_Renderer` it is handed, using
`geometry->rect`/`draw_text` helpers already used for the `color_blocks` overlay). Everything is
in `w_terrain`'s pixel rectangle, so overlays (inventory, ImGui) draw above it.

* **Compass strip** (top centre, 40 % of the width, ~24 px): a scrolling tape with `N E S W` and
  `NE SE SW NW` labels; heading at the centre mark. Ticks:
  * visible hostile creatures: red tick, height ∝ 1/distance, filled when adjacent;
  * visible non-hostile creatures / NPCs: grey/green tick (toggle `R3D_FP_COMPASS_ALL`);
  * sound markers of this turn: yellow `?`;
  * the look/target cursor when active: white caret;
  * the auto-travel destination: blue caret.
  Bearing comes from `direction_from(you.pos_bub(), p)` → angle, relative to `heading·45°`.
* **Edge alert**: when a hostile is adjacent and outside the frustum, a 6 px red bar on the
  screen edge nearest its bearing (left/right/bottom). Honours `R3D_FP_EDGE_ALERT`.
* **Crosshair**: a 1-tile cursor box in the scene (the existing `cursor` marker, added by r3d
  itself at the crosshair tile when `R3D_FP_CROSSHAIR` is on) plus a small 2D dot at the view
  centre. Below it, the **hover label**: terrain/furniture name, top item (`+N more`), creature
  name — the same text `game::handle_mouseview`/`liveview` already produce for the mouse, called
  with the crosshair tile when the mouse is not over the terrain window.
* **Message log and sidebar**: untouched; they are separate curses windows/panels.
* **Pixel minimap**: untouched (`w_pixel_minimap`); the option text recommends it. A later
  nicety rotates it with the heading (`PIXEL_MINIMAP_MODE` is a plain blit today; open
  question 6).

### 3.8 Looking, examining and targeting via the centre ray

* `r3d::crosshair()` (new, `r3d.h`): the `pick()` result for the viewport centre, computed once
  per drawn frame in `draw()` (uses the same `column_height` + creature lambda as `pick`). Also
  returns the hit creature if any. Cached in `state::crosshair`.
* **Look** (`game::look_around`): FP `draw()` keeps the eye on the player (F2) and draws the
  cursor marker at `lp`. Arrow/numpad keys move `lp` by `get_direction(action, true)`, which is
  already rotated by `view_eighths()`, so "right" moves the cursor to screen-right. Space
  (`r3d_fp_select_ahead`) sets `lp = crosshair`. Mouse hover and click work via `get_coordinates`.
  Fast scroll etc. unchanged.
* **Examine / open / close / smash / `choose_adjacent`**: when `R3D_FP_USE_HEADING` is on, the
  direction prompt pre-selects the heading tile (`choose_adjacent` is given an `initial`
  direction; new optional parameter defaulting to none). The player can still press any
  direction key; the keys are rotated by the heading as everywhere else. Pressing the action key
  twice (e.g. `e e` with Q/E turning off, the default) therefore examines what is ahead.
* **Fire / throw** (`target_ui` in `ranged.cpp`): the aim cursor starts on the last target as
  now; Space snaps it to the crosshair tile; the trajectory `animation_line` becomes posts
  (§3.6). `target_ui::set_view_offset` is ignored by the FP camera (F2). The aim window stays a
  curses overlay.
* **Mouse "look under cursor"** keeps working through `pick()`; the hover label uses the mouse
  tile when the mouse is in the terrain window, else the crosshair tile.

### 3.9 Z-levels, stairs and vehicles

**Z-levels**

* The camera's level is the player's. `u.view_offset.z()` (look-level keys, look mode's `<`/`>`)
  does not move the eye; it tilts `look_pitch` (±45°) and tells the game which level's cache to
  build (unchanged in `game::draw_ter`). A HUD line "Looking: 1 level up" is drawn while
  `view_offset.z != 0`.
* Standing surface: `surface_height()` of the player's tile (counter tops, benches) raises the
  eye; ramps use the terrain's own height (`shape.height`) if non-zero.
* Taking stairs (`>`/`<`) moves the player a level; the tween eases the vertical change over
  `R3D_FP_TWEEN_MS` (C1). Holes: looking down (`pitch`) shows `z_lo` levels through the
  `R3D_VIEW_DEPTH` geometry as now.
* Climbing/ladders/ropes: unchanged game actions.

**Vehicles** (milestone 8, new `r3d_vehicles.cpp`)

* Mesh: for every `vehicle` in `here.get_vehicles()` within the frustum, one box per part:
  footprint 1 tile rotated by `veh->face.dir()` about the vehicle's pivot, height by part class
  (frame 0.3, seat 0.6, boards/doors 1.0 (block), roof as a `face_ceiling`-like lid when the
  vehicle has roof parts), textured with the part's sprite (`cata_tiles` already resolves
  vehicle part sprites with rotation; use the unrotated sprite and rotate the quad). Rebuilt per
  frame into the dynamic buffers (vehicles move), or cached per vehicle by
  (`veh->face`, `veh->pos`, `parts` stamp).
* Picking: `column_height()` adds `here.veh_at(p)` → part height so the crosshair/mouse hits
  vehicles.
* Camera while driving: `R3D_FP_DRIVE_FOLLOWS_VEHICLE` (default on): yaw = `veh->face.dir()`
  continuously (not snapped; a vehicle's heading is not an eighth), eye over the driver's part at
  seat height; `forward_eighths()` returns the nearest eighth so any walking directions still
  rotate sensibly. `driving_view_offset` is ignored by the camera. Q/E are ignored; glance works.
  When the option is off, the vehicle turns under a fixed heading (nauseating; off by default
  for a reason, kept for comparison).
* Until milestone 8: FP shows a HUD line "vehicle not drawn (3D)" when `veh_at(player)`.

### 3.10 Tweening

* `struct fp_tween { vec3 eye_from, eye_to; float yaw_from, yaw_to; float pitch_from, pitch_to;
  uint64_t start_ms; uint32_t length_ms; }` in `state`. `camera::view()` in FP takes the
  interpolated eye/yaw (ease-out cubic) when a tween is live; the *logical* heading and player
  position are always the end values, so `view_eighths()`, picking and the HUD never lag.
* Started by: a player position change between frames (`fp_prev_pos != you.pos_bub()` and the
  step is ≤ 1 tile; teleports/stairs further than that snap), a heading change, a surface-height
  change, glance release.
* Length: `R3D_FP_TWEEN_MS` (0–250, default 100). Forced 0 while `you.is_auto_moving()`, while
  `you.activity` is running, when `ANIMATIONS` is off, and while `R3D_FP_TWEEN_MS == 0`.
* Redraw loop: the game only redraws on input. Add `r3d::animating()` (true while a tween or a
  glance snap-back is live) and, in `CheckMessages` (`sdltiles.cpp`, the same place
  `r3d_dragging` forces a redraw at line ~6408), when it is true and no event is pending,
  invalidate the main UI adaptor and redraw at ~60 Hz with a short wait (`SDL_WaitEventTimeout`
  of 16 ms). Input is still read every iteration, so nothing is buffered or dropped (M1).
* Entities are not rebuilt during a tween unless the game state changed (P2): `draw()` keeps the
  last `chunk_mesh` for entities and only re-uploads when `light_key` or the creature set changed.

### 3.11 Options (`options.cpp`, graphics page, after `R3D_CUTAWAY`; all prerequisite `RENDERER_3D`)

| Id | Type / range | Default | Note |
|----|--------------|---------|------|
| `R3D_CAMERA_MODE` | `orbit` / `first_person` | `orbit` | Also toggled by `r3d_first_person`. Text states that vision is unchanged and the HUD shows what is behind you. |
| `R3D_FP_FOV` | int 60–110 | 90 | Horizontal degrees. |
| `R3D_FP_EYE_HEIGHT` | int 50–95 | 80 | Percent of a tile width above the standing surface (stretched). |
| `R3D_FP_TWEEN_MS` | int 0–250 | 100 | 0 = instant steps and turns. |
| `R3D_FP_QE_TURN` | bool | false | Q/E turn in DEFAULTMODE. Off because `e` is examine in the default keymap (`q` is free); the text says examine then needs a rebind or the action menu. |
| `R3D_FP_USE_HEADING` | bool | true | Direction prompts pre-select the tile ahead. |
| `R3D_FP_CROSSHAIR` | bool | true | Cursor box + hover label at the centre ray. |
| `R3D_FP_COMPASS` | `off` / `heading` / `threats` | `threats` | Compass strip detail. |
| `R3D_FP_EDGE_ALERT` | bool | true | Red edge bar for adjacent off-screen hostiles. |
| `R3D_FP_OCCLUDE_UNSEEN` | bool | true | Never-seen tiles are black and solid instead of absent. |
| `R3D_FP_VIGNETTE` | int 0–50 | 0 | Edge darkening in percent (comfort). |
| `R3D_FP_DRIVE_FOLLOWS_VEHICLE` | bool | true | See §3.9. |
| `R3D_FP_WHEEL` | `none` / `fov` / `look_level` | `none` | Mouse wheel meaning in FP. |

Existing options in FP: `R3D_VIEW_DEPTH` used; `R3D_LEVEL_HEIGHT` used (geometry stretch);
`R3D_WASD` used (and extended with Q/E, ↑↓ pitch, ←→ turn); `R3D_WALL_TEXTURES` used;
`R3D_CUTAWAY` ignored. `PIXEL_MINIMAP` recommended on.

---

## 4. Milestones

Each milestone is independently testable in the game and with the existing tooling
(`tools/r3d_spike/run_pure_tests.sh`, `run_ingame.sh`, `CATA_R3D_SCREENSHOT`,
`CATA_R3D_CAMERA`). None changes game rules.

### FP0 — Minimal first-person camera, no gameplay changes
* `camera_mode`, `heading`, `eye_height`, FP `eye()/view()/projection()/forward_eighths()`;
  `R3D_CAMERA_MODE` option; `r3d_first_person` toggle (ctrl+F); `near = 0.05` in FP; cutaway,
  pull, pan and ortho disabled in FP; `drag_orbit` turned into glance; `r3d_rotate_*` turn the
  heading. Nothing else.
* Verify: pure tests — add `camera_fp` cases to `tools/r3d_spike` (eye at `(0.5,-0.5,surface+0.8·z_scale)` for a
  centred player; `forward_eighths()` equals heading for all 8; `screen_ray(0,0)` points along
  the heading; frustum contains a point 3 tiles ahead and excludes one behind). In game: toggle
  with ctrl+F in a house; ctrl+←/→ turns by 45° and numpad 8 walks the way you face; adjacent
  walls are not clipped; the picture is not orthographic; `CATA_R3D_SCREENSHOT` from
  `run_ingame.sh` with `CATA_R3D_CAMERA=...,fp` saves a plausible eye-level shot.

### FP1 — Upright billboards, items and markers
* `entity_view::upright_billboards`, vertical `emit_billboard`, `emit_item_stand`, upright
  markers/posts, stairs uprights, player sprite skipped, near-eye slide.
* Verify: spawn a zombie adjacent and 5 tiles away (debug menu): both stand upright, feet on the
  floor, no top lean, none clipped; drop an item: visible as a small stand; `x` look mode shows
  the cursor box; fire mode shows posts along the line. Orbit mode screenshot unchanged
  (regression: `CATA_R3D_COMPARE`).

### FP2 — Controls
* `r3d_key_action` FP branch (Q/E/arrows), `r3d_fp_select_ahead` in LOOK/TARGET,
  `r3d_fp_face_threat`, `R3D_FP_USE_HEADING` pre-selection in `choose_adjacent`, look-level
  keys, wheel option. Keybinding JSON + action enum + names.
* Verify: with WASD on, W/S/A/D step forward/back/strafe at all 8 headings (walk a square and
  come back to the start tile; check the message log shows no wasted turns on Q/E); in look mode
  Space selects the tile under the crosshair; Tab faces a spawned zombie; `e` with the option on
  pre-selects the tile ahead; keybindings menu lists the new actions; menus (inventory, crafting)
  still receive plain q/e/arrows.

### FP3 — Ceilings and sky
* `face_ceiling`, ceiling atlas cell, `mesh_options.ceilings`, `z_hi` always above in FP,
  sky clear colour by daylight, chunks rebuilt on mode switch.
* Verify: inside a house look up (ctrl+↑): a lit ceiling, no black sky; under a porch roof the
  ceiling ends at the roof edge; outdoors the sky colour follows day/night; orbit mode shows no
  dark ceiling artefacts (screenshot compare); `run_pure_tests.sh` extended with
  `visible_faces` ceiling cases (floor under floor → ceiling; floor under empty → none; block →
  none).

### FP4 — Tweening and the redraw loop
* `fp_tween`, `r3d::animating()`, `CheckMessages` 60 Hz redraw while animating, auto-move /
  activity suppression, entity rebuild cache.
* Verify: holding numpad 8 walks continuously with no dropped steps (count tiles vs. key
  repeats); `R3D_FP_TWEEN_MS = 0` gives instant steps; auto-travel across a field is not slowed
  (time a 30-tile travel with and without FP); CPU use idle when nothing animates (no busy
  loop: check with `top`).

### FP5 — HUD
* Compass strip, threat ticks, edge alert, crosshair dot + hover label, "Looking: level" line,
  sound markers as posts/ticks.
* Verify: a zombie behind you shows as a red tick opposite the centre and the edge bar lights
  when adjacent; a smashed window behind you shows a `?` tick for one turn; hover label names the
  furniture ahead; overlays (inventory, ImGui windows) draw above the HUD.

### FP6 — Visibility polish
* `R3D_FP_OCCLUDE_UNSEEN` shader flag, linear-min sampler for terrain batches, far plane 200,
  front-to-back chunk order, item frustum pre-filter.
* Verify: unexplored rooms behind a known wall are black and solid, not see-through; a highway at
  dusk renders ≥ 30 fps at 1080p with `CATA_R3D_SUPERSAMPLE=2` (log frame time with
  `CATA_R3D_STATS=1`, new env); floors far away no longer sparkle.

### FP7 — Z-levels
* Surface-height eye, look-level tilt + HUD line, stairs easing, holes through `R3D_VIEW_DEPTH`.
* Verify: stand on a counter (eye rises), take stairs down (smooth vertical ease, correct level
  drawn), look up a stairwell, stand at a 2nd-floor window and look down at the street.

### FP8 — Vehicles
* `r3d_vehicles.cpp` boxes, picking through `veh_at`, camera follows vehicle while driving.
* Verify: a parked car is visible and pickable (examine via crosshair opens the part menu); drive
  a loop: the horizon turns with the car, steering keys unchanged, speed/turn messages as in 2D;
  passengers see the interior parts; a car crash animation does not stall.

### FP9 — Comfort and options pass
* Vignette, FOV wheel, option texts, `doc/R3D_STATUS.md` update, keybinding help entries.
* Verify: options page shows all `R3D_FP_*` with prerequisites; defaults produce no head bob;
  FOV 60 and 110 both render without near-plane holes; a 20-minute play session by the two
  users with notes on nausea and awareness (the real test).

---

## 5. Open questions for the user

Decided 2026-10-17 by the user (their friend is the other intended player):

1. Vision stays 360°; no cone option.
2. Turning keys: the arrow keys. No Q/E binding; examine stays on `e`.
3. 45° turns; no 90° option.
4. Step tween: **instant** by default (`R3D_FP_TWEEN_MS = 0`); the tween stays available for
   trying out later.
5. Vehicles (FP8) come after the HUD (FP5) and z-levels (FP7); not needed for the first
   playable.
6. Minimap: **rotates with the heading**, GTA-style (heading up, map turns under a fixed
   player marker). The pixel minimap (`pixel_minimap.cpp`) needs a rotated draw mode for this;
   it is north-up today.

Questions 7-10 below are still open and keep their proposed defaults.

The original questions, for the record:

1. **Rules stay 360°?** This design keeps full-circle vision and compensates with HUD cues. Do
   you ever want a "hardcore" option where the game's vision becomes a cone (that is a rules
   change touching `map::build_seen_cache` and every `you.sees()` caller, and would be a separate
   design)?
2. **Turning keys.** Arrow keys already turn/tilt the camera when `R3D_WASD` is on, so that is
   the proposed primary binding. Q/E (the blobber convention) collide with `e` = examine in the
   default keymap (`q` is free; `E` is eat, `[`/`]` are mutations/spells, `.` is pause). Options:
   (a) arrows only; (b) Q/E behind the off-by-default `R3D_FP_QE_TURN` with examine rebound;
   (c) another free pair you prefer. Which, and are you willing to rebind examine?
3. **45° or 90° turns?** 45° matches the game's 8 directions and avoids zig-zags; 90° is the
   classic blobber feel and halves the orientation states. Default 45°, with a `R3D_FP_TURN_90`
   option? Or 45° only?
4. **Persist heading in the save?** Storing it in `uistate` (per save) is cheap; otherwise you
   load facing north.
5. **Mouse wheel**: FOV zoom, look-level, or nothing in FP?
6. **Minimap**: is the existing pixel minimap enough, or do you want it rotated to the heading
   (north-up vs heading-up is a strong personal preference)?
7. **Step tween default**: 100 ms, or instant (purist roguelike feel)? Both users should try
   both in FP4.
8. **Vehicles priority**: FP8 is the largest milestone. Is driving in first person needed for the
   first playable, or can it come after FP5 (HUD) and FP7 (z-levels)?
9. **Glance commit**: should releasing a mouse glance snap the heading to the nearest eighth
   (Daggerfall-like) or always snap back to the old heading (MMX-like, proposed default)?
10. **Where does "forward" point after loading an orbit save or switching modes?** Proposed: the
    orbit camera's `forward_eighths()` at the moment of switching, so the view does not jump.

---

## 6. Sources

Movement / input
* pancelor, *Input buffering for grid games* — https://pancelor.bearblog.dev/input-buffering-for-grid-games/ (site unreachable from this session; summarised from search excerpts)
* modd.io, *Grid Based Movement (+ input buffer)* — https://modd.io/forum/t/grid-based-movement-input-buffer/4533
* lutzgrosshennig, *unity3d-advanced-grid-movement* (command queue, animation curves) — https://github.com/lutzgrosshennig/unity3d-advanced-grid-movement
* bit-tech, *Legend of Grimrock review* ("clunky when in a hurry") — https://bit-tech.net/reviews/gaming/pc/the-legend-of-grimrock-review/2/
* Softpedia, *Legend of Grimrock Diary* (no diagonals; mouse looks but does not turn) — https://games.softpedia.com/blog/Legend-of-Grimrock-Diary-Walls-and-Soft-Modern-Gamers-268047.shtml
* GOG forum, *Grimrock controls* (WASD strafe, QE turn) — https://www.gog.com/forum/legend_of_grimrock_series/controls
* GameBanshee, *Might & Magic X Legacy gameplay footage and info tidbits* (Limbic Q&A: WASD+QE, right-mouse free-look snaps back) — https://gamebanshee.com/ka3u6
* Might & Magic X Legacy manual (rotating does not advance time) — https://mocagh.org/nwc/mm10-manual.pdf
* GodisaGeek, *MMX review* (grid feels rigid outdoors) — https://godisageek.com/2014/02/magic-legacy-review/
* PC Gamer, *MMX review* (ranged attacks "around a corner") — https://www.pcgamer.com/might-and-magic-x-legacy-review
* PCGamesHardware forum, MMX thread (dislike of 90°-only turning) — https://extreme.pcgameshardware.de/threads/might-magic-x-legacy-finaler-release-im-januar.311590/

Comfort / camera
* Xbox Accessibility Guideline 117 (camera motion, FOV, disable automatic camera movement) — https://devdocs.xbox.com/gaming/accessibility/xbox-accessibility-guidelines/117
* Kingdom Come forum, *Head bobbing motion sickness* — https://forum.kingdomcomerpg.com/t/head-bobbing-motion-sickness/39868
* Steady Sight mod (eased vertical steps, FOV restriction note) — https://modrinth.com/mod/steady-sight
* No Man's Sky thread (wide FOV side effects) — https://steamcommunity.com/app/275850/discussions/0/1762481957310661227
* Kelly, Doty, Gilbert, Dorneich, *FOV restriction and snap turning* (N=201) — https://par.nsf.gov/servlets/purl/10591601
* Farmani & Teather, *Viewpoint snapping to reduce cybersickness* (GI 2018) — https://www.csit.carleton.ca/~rteather/pdfs/GI_2018_viewpoint_snapping.pdf
* *Evaluating discrete viewpoint control to reduce cybersickness* — https://paper.nweon.com/6664 ; arXiv 2502.15227 — https://www.arxiv.org/pdf/2502.15227
* dfworkshop forum (mouse smoothing input lag) — https://forums.dfworkshop.net/viewtopic.php?p=30251
* Daggerfall Unity 0.10.23 (keyboard turning wired up) — https://www.dfworkshop.net/daggerfall-unity-0-10-23/
* UESP, *Daggerfall Unity settings* — https://en.uesp.net/wiki/Tes2Mod:Daggerfall_Unity/Settings

Rendering
* SIGGRAPH '98 Advanced OpenGL notes, *Billboards* (cylindrical vs spherical) — https://www.opengl.org/archives/resources/code/samples/advanced/advanced98/notes/node42.html
* Lighthouse3D, *Billboarding tutorial* — https://www.lighthouse3d.com/opengl/billboarding
* Unreal AnswerHub, near plane vs depth precision — https://answers.unrealengine.com/questions/5074/view.html
* Khronos forum, *OpenGL camera glitch* (near plane and walls) — https://community.khronos.org/t/opengl-camera-glitch/106162
* bugnet.io, *How to fix near plane clipping into geometry (first person)* — https://bugnet.io/blog/how-to-fix-near-plane-clipping-into-geometry-first-person
* SIGGRAPH '99 notes, depth buffer resolution — https://ii.uni.wroc.pl/~anl/cgfiles/Sig99AdvOpenGLnotes/node28.html
* Roblox rendering update (float depth, reversed-Z) — https://devforum.roblox.com/t/rendering-updates-april-2017/35340/17

Prior art
* The Digital Antiquarian, *Life on the Grid* (blobbers, why the grid survived) — https://filfre.net/?p=3803
* Legend of Grimrock on Steam (automap, Oldschool Mode) — https://store.steampowered.com/app/207170
* MobyGames, *Eye of the Beholder* (rear attacks on the back row) — https://www.mobygames.com/game/835/
* NotEye / Necklace of the Eye (hidden FPP mode for roguelikes) — https://www.roguetemple.com/z/noteye
* itch.io thread on first-person roguelikes losing overview — https://itch.io/post/6884902
* ROGUE-FP (Steam; r/roguelikes thread on its time-based system) — https://store.steampowered.com/app/2343160/ROGUEFP/ ; https://www.reddit.com/r/roguelikes/comments/1ostn75/roguefp_version_37_first_person_traditional
* Falcon's Eye (Wikipedia) — https://en.wikipedia.org/wiki/Falcon%27s_Eye ; Vulture (nethackwiki) — https://nethackwiki.com/wiki/Vulture ; LinuxLinks Falcon's Eye — https://www.linuxlinks.com/FalconsEye/
* Giant Bomb, *Barony* (first person + real time as departures from roguelike design) — https://www.giantbomb.com/wd/3030-48929
* Daggerfall Unity 1.0 release notes — https://www.dfworkshop.net/category/releases/

In-repo references used throughout: `src/r3d.cpp` (`draw`, `pick`, `set_cutaway`, `light_sampler`,
`open_sky` logic), `src/r3d_camera.{h,cpp}` (`camera`, `near_plane`, `forward_eighths`),
`src/r3d_entities.cpp` (`emit_billboard`, `emit_flat`, `emit_marker_box`), `src/r3d_shapes.cpp`
(`visible_faces`), `src/r3d_mesh.{h,cpp}` (`light_ref`, `quad_sink`, wall sides),
`src/r3d_gpu.cpp` (depth format, cull mode, sampler), `data/shaders/r3d_block.{vert,frag}`,
`src/sdltiles.cpp` (draw hook ~4118, middle-drag ~6408, `get_coordinates` ~7427),
`src/input_context.cpp` (`r3d_key_action`, `get_direction`), `src/action.cpp`
(`rotate_eighths`, `get_delta_from_movement_action`), `src/handle_action.cpp` (movement and
`ACTION_R3D_*` dispatch, `pldrive` with `iso_rotate::no`), `src/game.cpp` (`draw_ter`,
`look_around`, `zoom_in/out`), `src/options.cpp` (`R3D_*`), `data/raw/keybindings.json`.
