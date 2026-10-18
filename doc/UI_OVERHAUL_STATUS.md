# UI overhaul: handoff status

Branch `feature/ui-overhaul`. The plan and the reasoning behind it are in `doc/UI_RESEARCH.md`
(its Status section lists every item with its option and how to try it, plus "What was cut and
why"). This file covers where the work stopped and how to carry on.

Guiding rules from the user: in-world presentation may replace sidebar text, but every piece of
state stays available as text on demand (status report, log messages, classic sidebar presets).
Be ruthless: one signal per piece of state, few options, cut what duplicates. Nothing changes when
`RENDERER_3D` is off. No game-rule changes. Classic menu style stays selectable.

## Done

| Item | Commit | Notes |
|---|---|---|
| 1 Weather in 3D (particles from tileset sprites, dithered; wind drift; fog by sight penalty; sky colour by time and cloud; lightning flash; weather and wind change messages in the log) | `2d662888` | Option `R3D_WEATHER` (also controls sky and sun/moon) |
| 2 Overlays the 3D path skipped: combat text, heard-sound footsteps, zone placement | `2d662888` | Option `R3D_HUD` (all marks drawn over the picture) |
| 3 Sun and moon discs (sundial maths, `get_moon_phase`, 8 phase sprites) | `98feeea6` | Seen in shot `sun4_3d.png` |
| 4 Edge arrows for seen hostiles (red) and heard sounds (yellow) | `98feeea6` | Not checked on screen (no hostile in the test save) |
| 5 Compass tape: N/E/S/W, sun/moon, local wind, mission, travel destination, near notes | `98feeea6` | Seen in shots `sun3`/`sun4`/`hud1` |
| 6 "3D" sidebar preset `data/json/ui/sidebar-r3d.json` + status report action (F4, action menu → Info) | `98feeea6` | The JSON loads without errors; the preset and F4 were not tried in game |
| 7 Status badges from widget `r3d_status_row` | `98feeea6` | Not checked on screen (the test character has no abnormal needs) |
| 8 Hit direction (red edge glow towards the nearest seen hostile) | `45921cd5` | Not checked on screen |
| 9 "Entering: <place>" notice and auto-travel route ribbon | `45921cd5` | Not checked on screen |
| Menus 1: survey (ImGui vs curses), in `UI_RESEARCH.md` | `bfd68bde` | |
| Menus 2: modern style (ImGui sizes/palette, key caps, curses border/title colours), selectable; classic stays the default | `b8e506f1` | Main menu checked on screen (`new_modern.png` vs `new_classic.png`) |
| Menus 3 (part): clearer New Game labels and hints (same hotkeys), "Menu style" in Settings | `b8e506f1` | |

## Partly done

* **Menus 2: the ImGui style has not been seen on screen.** The script sent `s` then `y` to
  open the keybindings screen, but it stayed on the main menu, so `keys_modern.png` shows the
  main menu. The C++ compiles and the JSON parses at startup, but rounding, padding and key caps
  have not been seen. Next step: open any ImGui screen (e.g. Help with `?`, or Settings → Keybindings
  with arrow keys + Return) on the private Xvfb using `menu_shot.sh` (see below) and compare the
  classic and modern styles.
* **Menus 3: the menus have not been simplified yet.** Still to do: options screen navigation
  (`src/options.cpp`, curses), inventory and advanced inventory headers (`src/inventory_ui.cpp`,
  `src/advanced_inv.cpp`), crafting filters (`src/crafting_gui.cpp`, ImGui). Only the main menu
  was changed.
* **Menus 4 (accessibility review)** was not done. ImGui text stays text. The key caps draw
  the key with `AddText` on the draw list instead of a text item. Check that this does not hide
  the key from any text-scraping or screen-reader path the game has (`cataimgui` has no such
  path that I found). If it does, draw the key with `ImGui::Text` over the frame instead.
* **Doc Status section:** `UI_RESEARCH.md` lists items 1-10 and the menu survey. It does not yet
  have the menus before/after notes. Add them from this file.

## Not started

* Item 10, horizon landmarks: deliberately not built (see "What was cut and why" in
  `UI_RESEARCH.md`).
* `R3D_UI_TEXT_FALLBACK` / automatic screen-reader detection: not built. The classic sidebar
  presets and the status report cover the text side. Document it or build it if the user wants it.
* First-person versions of these cues: they belong on `feature/r3d-first-person`, which this
  branch does not contain.

## Known bugs, risks and regressions

* **Partial rebuild after `action.h`.** `ACTION_STATUS_REPORT` was added just before
  `NUM_ACTIONS`, and only `action.o` and `handle_action.o` were rebuilt, the only files that use
  `NUM_ACTIONS`. A clean build is unaffected. If a build of this branch crashes in action
  handling, delete every object that includes `action.h` and rebuild.
* **Objects copied from the main checkout.** The worktree's `obj/tiles` was seeded from
  `/home/user/Cataclysm-DDA-3D/obj/tiles` to skip a full build. Objects for every changed file
  were deleted and rebuilt. If the binary behaves oddly, run `make clean` once.
* **Particles animate only when the game redraws.** While it rains with `ANIMATIONS` on that is
  every 125 ms, as in 2D. Snow under another weather type without a tiles animation will stand
  still.
* **The sky colour is rarely visible in orbit mode at the default pitch.** Never-seen ground is
  drawn as dark floor, so the sky shows only at low pitch beyond the loaded area. The compass
  tape carries the sun and moon at any pitch.
* **The hit direction is a guess.** `character_takes_damage` does not name the attacker, so the
  glow points at the nearest hostile the player sees. A hidden attacker or a trap gives the
  all-round glow.
* **The event subscriber is leaked on purpose.** It is a leaked singleton in `r3d_hud.cpp`
  `events()`. It subscribes on the first 3D frame, and it subscribes again if the event bus
  address changes. If a new bus is ever allocated at the same address, the notices stop until
  restart.
* **The moon is placed as the sidebar's sundial places it,** opposite the sun with mirrored
  altitude. That is not astronomically correct (the sundial code says so too).
* **The weather-change messages are new log lines** (m_info), shown only while 3D is active and
  the sky is visible.
* **The New Game labels and hints were rewritten.** The translation strings changed, so
  existing translations of these lines are lost.
* **The particle sprites are cropped to the sprite's opaque box.** A tileset whose rain sprite
  has no opaque info uses the whole cell.

## How to build and test

```
cd <worktree>
export PKG_CONFIG_PATH=/opt/sdl3/lib/pkgconfig LD_LIBRARY_PATH=/opt/sdl3/lib
make TILES=1 RELEASE=1 LINTJSON=0 ASTYLE=0 LOCALIZE=0 BACKTRACE=0 CCACHE=1 -j4 cataclysm-tiles
bash tools/r3d_spike/run_pure_tests.sh          # 25 cases incl. new sky/wind/particle/fog/bearing/edge tests
glslangValidator -V data/shaders/r3d_block.vert -o /dev/null
glslangValidator -V data/shaders/r3d_block.frag -o /dev/null
```

Dependency files do not list headers. After editing a header, delete the objects of every file
that includes it, transitively. A helper that lists them:
`/tmp/claude-0/-home-user-Cataclysm-DDA-3D/202dc167-326b-590f-8571-65d2c78e2cac/scratchpad/tools/includers.py src <header.h>`.
The hub headers to watch are `cata_tiles.h` (included by `map.h`, so about 190 files), `action.h`
(about 255 files), `r3d.h` (6 files) and `r3d_hud.h`/`r3d_sky.h`/`r3d_entities.h` (r3d files only).

Screenshots (private Xvfb, software Vulkan; about 3 minutes each; they never touch a real display):

* In game, 3D view:
  `/tmp/claude-0/-home-user-Cataclysm-DDA-3D/202dc167-326b-590f-8571-65d2c78e2cac/scratchpad/tools/shot.sh <name> [ENV=VAL ...]`
  copies the user dir `ud` to `ud_ui` and saves `scratchpad/shots/<name>_3d.png`. These debug
  variables change only what the 3D view draws:
  * `CATA_R3D_WEATHER=<weather id>`
  * `CATA_R3D_TIME_SHIFT=<hours>`
  * `CATA_R3D_FULLBRIGHT=1` (shows the outdoors; the test character stands in a windowless shelter)
  * `CATA_R3D_CAMERA=yaw,pitch,dist`
  * `CATA_R3D_DEBUG_LAYERS=1` (logs particle info to `config/debug.log`)
* Main menu and other menus:
  `scratchpad/tools/menu_shot.sh <name> <classic|modern_style.json> [xdotool keys...]`.
  It waits about 2 minutes for the menu, presses the keys, and saves `scratchpad/shots/<name>.png`.
* Shots taken so far, in `scratchpad/shots/`:
  * `rainfb3_3d.png` (rain), `snow_3d.png` (snow, fog, cloudy sky)
  * `sun_3d.png`, `moon_3d.png` (day and night sky)
  * `sun3_3d.png`, `sun4_3d.png` (compass tape, sun disc at dawn)
  * `hud1_3d.png` (compass)
  * `new_classic.png`, `new_modern.png` (New Game submenu)
  * `main_modern.png`

In-game manual checks still owed: F4 status report text; the sidebar preset "3D"; edge arrows;
badges; hit glow; "Entering" notice; travel ribbon; the modern style on ImGui screens.

## Continuation plan (in order)

1. **Verify the modern ImGui style on screen.** Extend `menu_shot.sh` with keys that reach Help
   (`question`) or Settings → Keybindings (`s`, then `Down` and `Return`). Compare classic and
   modern. Tune `data/raw/imgui_styles/modern_style.json`. If the key caps clash, adjust
   `cataimgui::TextKeybinding` in `src/cata_imgui.cpp`.
2. **Load a save with a hostile in view** (or use a debug-spawned world copy) and check the edge
   arrows, hit glow, combat text and badges. All of them are in `src/r3d_hud.cpp`.
3. **Simplify the menus (step 3)**, how they read only:
   * `src/options.cpp`: page/tab header and the help line.
   * `src/inventory_ui.cpp`: header lines.
   * `src/advanced_inv.cpp`: pane headers.
   * `src/crafting_gui.cpp`: the filter hint line, which should become a short key-cap legend.
   * Gate visual changes on `get_ui_theme()` (`src/ui_theme.h`) so the classic style stays
     unchanged.
4. **Accessibility pass (step 4).** Keyboard-only walk-through of the changed menus, and confirm
   all text is still text.
5. **Add before/after notes for the menus** to the Status section of `doc/UI_RESEARCH.md`,
   with the screenshots.
6. **Optional:** add `R3D_UI_TEXT_FALLBACK`, or document how to switch to a classic sidebar.
   Port the compass, arrows and badges to the first-person branch once it merges.

Pitfalls hit:

* **This agent sandbox refuses some compound shell commands.** It rejects a heredoc piped into
  python together with other commands. Write edit scripts to files with the Write tool and run
  them. Run long jobs in the background and wait with an until-loop, not `sleep`.
* **The particle sprites were invisible at first.** The 3D pass has no blending and the shader
  discards alpha < 0.75. Particles are therefore drawn as a separate mesh with `fog2.z = 1`, the
  shader's ordered dithering.
* **`block_uniforms` is now 160 bytes.** Keep `src/r3d_gpu.h` and both `r3d_block` shaders in step.
* **`entity_view` is aggregate-initialised positionally in `r3d.cpp`.** Adding members means
  updating that initialiser.
* **`menu_shot.sh` must delete the copied `debug.log`** before waiting for "Loaded tileset",
  or it stops waiting at once.
