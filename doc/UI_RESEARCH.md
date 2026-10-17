# UI research: what the 3D view should show, and what stays text

Research note for the 3D renderer (`feature/r3d-renderer`, see `doc/R3D_STATUS.md`) and the
first-person mode (`feature/r3d-first-person`, `doc/R3D_FIRST_PERSON.md` on that branch).
Written 2026-10-17. No code was changed for this note.

**How the sources were read.** GitHub pages of CleverRaven/Cataclysm-DDA were read directly (issue
and PR threads). The CDDA Discourse forum, Reddit, Steam, PC Gamer, GamingOnLinux, TouchArcade and
YouTube could not be opened from this session (DNS failures or blocked hosts), so for those the
note relies on search-engine summaries of the pages and says so where it matters. Treat claims
sourced that way as "a page says", not as checked quotes. Player opinion from Reddit and Steam
reviews is therefore thinner than planned; see the open questions at the end. The repo itself
(`data/json/ui/`, `doc/WIDGETS.md`, `src/`) was read directly.

---

## Summary

* CDDA's players do not mostly complain that the sidebar has *too much*. They complain that it is
  laid out badly, that space is wasted, that it is hard to find the setting that changes it, and
  that some state cannot be seen at all (attacks from another z-level, turrets firing from beyond
  earshot, weather overhead). Whenever a redesign tried to hide information, veterans pushed back
  ("Nearly everything on the current sidebar is important in nearly all situations",
  [#27367](https://github.com/CleverRaven/Cataclysm-DDA/issues/27367)). So: **add, don't remove**.
  The sidebar stays. The 3D view takes over only what it can show better than text.
* The 3D view currently shows *less* than the 2D tiles view: going through `r3d::draw` skips
  `tilecontext->draw`, which is where the rain/snow animation (`draw_weather_frame`), scrolling
  combat text (`draw_sct_frame`), zone overlays and, in orbit mode, the footstep markers are drawn
  (`src/sdltiles.cpp`, the `drawn_3d` branch of `curses_drawwindow`). Putting those back is the
  cheapest win.
* What 3D games do well, and what we can borrow: sky and light *are* the clock (Minecraft,
  Valheim, The Long Dark); wind made visible in particles and foliage is both weather and
  direction (Ghost of Tsushima); a compass tape with markers on it is the standard answer to "which
  way, and what is over there" (Skyrim, Daggerfall Unity, Valheim's wind arrow); status icons that
  appear only when something is wrong (Project Zomboid moodles, BotW temperature gauge, Pro HUD);
  sound turned into directional marks (Fortnite's "Visualize sound effects", Minecraft subtitle
  mods); damage turning the screen edge red or desaturating it (DayZ).
* What they do badly: markers that turn exploration into following an arrow (Skyrim criticism);
  removing the map and markers entirely and leaving players lost (Outward and Kingdom Come
  hardcore mode, mixed reception); combat music that gives away unseen enemies (Skyrim); diegetic
  devices that are slower to read than a number (Far Cry 2 still had to fall back on a HUD for
  ammo and health).

---

## Part 1: CDDA's UI, as discussed by its players

### 1.1 What the sidebar holds today

The sidebar is data: 720 `"type": "widget"` objects across `data/json/ui/*.json`, drawn by
`src/widget.cpp` (2,039 lines) and laid out by `src/panels.cpp` (887 lines; the log, the
overmap chunk and the minimap are still C++ draw functions there). Shipped layouts:
`sidebar.json` (the "custom" layout), `sidebar-legacy-classic`, `-compact`, `-labels`,
`-labels-narrow`, `-one-padding`, `sidebar-mobile`, `sidebar-thick`, `sidebar-thick-cleaner`,
`spacebar/` and `structured/`.

The default "custom" layout (`data/json/ui/sidebar.json`) lists: HP graphs and numbers per body
part, stats, movement (move count, move mode, speed), stamina / activity / weariness, needs
(hunger, thirst, rest, warmth), safe mode and sound, bionic power, radiation badge, a compass of
visible creatures with a legend, wielded weapon and style, place / date / time, an overmap chunk
and location, lighting / moon / wind / temperature, vehicle azimuth / cruise / fuel, vital numbers
(pain, health, cardio), outer armour per body part, body part status (bleeding, bitten, broken),
a body graph, mana, oxygen. The log, minimap and overmap are added to every custom layout
([#51438](https://github.com/CleverRaven/Cataclysm-DDA/pull/51438)).

Two details matter for "show it in the world":

* Some sidebar values are already *diegetically gated*: `time_text` is "exact if clock is
  available, approximate otherwise", `env_temp_text` shows only "if thermometer is available",
  and `sundial_text` gives the position of the sun or moon (`doc/WIDGETS.md`, Variables). CDDA
  already thinks of time and temperature as things the character has to perceive.
* There is a "Look at the sky" action (`data/raw/keybindings.json`, id `sky`) from
  [#27947](https://github.com/CleverRaven/Cataclysm-DDA/pull/27947) (2019): a weather map of the
  sky within line of sight, so the player can "see sky/weather overhead for trying to predict
  weather changes". In the thread, commenters immediately asked for temperature, wind and cloud
  movement in the same view, a legend, and skill-gated forecasts.

### 1.2 How the UI got here

| Year | Change | Source |
|---|---|---|
| 2019 | "sidebar needs an overhaul": too much space, messy; long fight about panels vs density | [#27367](https://github.com/CleverRaven/Cataclysm-DDA/issues/27367), [#27439](https://github.com/CleverRaven/Cataclysm-DDA/pull/27439) (closed) |
| 2019 | Multiple layouts (classic, compact, labels), rearrangeable panels, **pixel minimap** | [#28152](https://github.com/CleverRaven/Cataclysm-DDA/pull/28152) |
| 2019 | Look at the sky / weather on the overmap | [#27947](https://github.com/CleverRaven/Cataclysm-DDA/pull/27947) |
| 2019 | Look Around tab improvement (62 comments) | [#27743](https://github.com/CleverRaven/Cataclysm-DDA/pull/27743) |
| 2022 | JSON "custom" sidebar, user-defined sidebars (widgets) | [#51438](https://github.com/CleverRaven/Cataclysm-DDA/pull/51438) |
| 2022 | Old C++ sidebars converted to JSON | [#58113](https://github.com/CleverRaven/Cataclysm-DDA/pull/58113) |
| 2022 | Structured sidebar mod merged into the game | [mlange-42/cdda-structured-sidebar](https://github.com/mlange-42/cdda-structured-sidebar) |
| 2023 | Spacebar sidebar | [#63502](https://github.com/CleverRaven/Cataclysm-DDA/pull/63502) |
| 2024 | ZenFS "thick" sidebar for wide screens ("None of the other default sidebars provide the player with as much information") | [#71109](https://github.com/CleverRaven/Cataclysm-DDA/pull/71109) |
| 2024 | ImGui framework (520 comments), then menu-by-menu migration: missions, item info, compare | [#65709](https://github.com/CleverRaven/Cataclysm-DDA/pull/65709), [#73134](https://github.com/CleverRaven/Cataclysm-DDA/pull/73134), [#73456](https://github.com/CleverRaven/Cataclysm-DDA/pull/73456), [#78557](https://github.com/CleverRaven/Cataclysm-DDA/pull/78557), [#78465](https://github.com/CleverRaven/Cataclysm-DDA/issues/78465) |

The trend is clear: CDDA answered "the sidebar is a mess" with **more choice, not less
information**. Every new preset shows *more* (thick sidebar), or the same in better order
(structured, spacebar).

### 1.3 Recurring pain points

Each item: what players say, evidence, and whether a 3D view can help.

1. **Layout and wasted space, not raw quantity.** "Sidebar take too much space ... The overall
   feeling is messy" (nsklaus); "the main problem is the limited vertical space" (rumly111); but
   also "if it's so dense you can't understand it, it's too dense" (kevingranade) against "displays
   a great deal more information in a smaller space, and requires no management" (Solusphere)
   ([#27367](https://github.com/CleverRaven/Cataclysm-DDA/issues/27367)). Panel redesigns were
   attacked for showing less and for squeezing the message log
   ([#27439](https://github.com/CleverRaven/Cataclysm-DDA/pull/27439)). Discourse users reported the
   newer sidebar pushing the minimap off the bottom of the screen (search summary of
   [Ya'll messed up the interface](https://discourse.cataclysmdda.org/t/yall-messed-up-the-interface/19310)).
   *3D:* the 3D view itself is a bigger picture of the same terrain window; it does not free
   sidebar space unless a 3D-specific preset drops what the world now shows.
2. **The UI is not taught, and its settings are hard to find.** "Toggle Panel Admin" is not
   something a player "could guess" (paulenka-aleh in
   [#28152](https://github.com/CleverRaven/Cataclysm-DDA/pull/28152)); "Neither Tutorial nor Play
   Now! ... explains UI to the player" (silencecoder in
   [#57571](https://github.com/CleverRaven/Cataclysm-DDA/issues/57571)); new players do not know
   to read the log to find out why safe mode stopped them (same thread). A Steam guide author spent
   20 minutes finding out how to boil water (search summary,
   [Steam guide](https://steamcommunity.com/sharedfiles/filedetails/?id=2955072583)).
3. **Hidden state: things happen that the screen does not show.** Attacks from another z-level
   looked like zombies fighting each other; "There should be indicators that enemies are
   attacking/being attacked from another Z level" (ThaniCF,
   [#42845](https://github.com/CleverRaven/Cataclysm-DDA/issues/42845); the fix was to the rules,
   no indicator was added). Turrets can "fire from farther away than their 'Hostile detected!'
   sound can be heard"; proposals were to give them back the laser pointer, a red dot "over you"
   before firing, loudspeaker warnings and "keep out" signs
   ([#35378](https://github.com/CleverRaven/Cataclysm-DDA/issues/35378)). *3D:* this is exactly
   what the world can show: a laser line, a red dot, a sign, an attacker drawn on the roof above.
4. **Interruptions and safe mode.** "A rat farted five meters down. Curl up in fetal position and
   cry? Y/N" ([#34621](https://github.com/CleverRaven/Cataclysm-DDA/issues/34621)): sounds from
   other z-levels interrupt long actions; players want "ignore all interruptions except for
   damage". See also "Extremely annoying (I)gnore"
   ([#28672](https://github.com/CleverRaven/Cataclysm-DDA/issues/28672)) and "Feature request NO
   WARNINGS" ([#10637](https://github.com/CleverRaven/Cataclysm-DDA/issues/10637)). *3D:* a
   visible cause (a tick on the compass, a marker on the tile) makes an interruption feel fair even
   when it still happens.
5. **Message log noise.** "Moving over this repeated message is slow"
   ([#28215](https://github.com/CleverRaven/Cataclysm-DDA/issues/28215)); the log is too small to
   follow combat when panels shrink it ([#27439](https://github.com/CleverRaven/Cataclysm-DDA/pull/27439)).
   *3D:* floating combat text and hit flashes move the combat half of the log into the world.
6. **Weather, time, light, wind, temperature as text only.** The sky view PR exists because the
   player could not see the weather coming
   ([#27947](https://github.com/CleverRaven/Cataclysm-DDA/pull/27947)); "Weather
   prediction/forecast?" ([#2633](https://github.com/CleverRaven/Cataclysm-DDA/issues/2633));
   "Standing near burning fire doesn't makes player warm" (a perception problem as much as a rules
   one, [#8300](https://github.com/CleverRaven/Cataclysm-DDA/issues/8300)); a numeric temperature
   was one of two things a thick-sidebar user missed
   ([#71109](https://github.com/CleverRaven/Cataclysm-DDA/pull/71109)). In 2D tiles the only
   in-world weather is a rain/snow glyph animation (`tiles_animation` in
   `data/json/weather_type.json`), and on curses it is buggy (search summary of a Discourse report
   that the rain animation shifts the map).
7. **Overmap vs local map, finding places.** The overmap is a separate full screen; the sidebar
   shows a small chunk of it. Players ask for square overmap fonts because north-south distances
   look wrong and want a symbol for unexplored tiles rather than a blank (search summary,
   [Good font for overmap?](https://discourse.cataclysmdda.org/t/good-font-for-overmap/15215)). A
   forum suggestion wants the minimap to hide what you cannot see at night or in forest
   ([Overmaps, GPS, Maps, Sight](https://discourse.cataclysmdda.org/t/overmaps-gps-maps-sight-and-positioning/12940)).
   *3D:* landmarks on the horizon and a compass tape carrying the destination and notes join the two
   maps up.
8. **Auto-travel and look mode friction.** Auto-drive prefers roads, slows to a crawl and spins;
   the reporter asked for "path visualization overlays" and confirmation-based auto-move
   ([#73062](https://github.com/CleverRaven/Cataclysm-DDA/issues/73062)). Look mode needed a
   62-comment rework ([#27743](https://github.com/CleverRaven/Cataclysm-DDA/pull/27743)).
9. **Menus in menus: inventory, crafting, construction.** Crafting "intimidated" some players for
   many playthroughs (search summary of
   [How hard was the learning curve](https://discourse.cataclysmdda.org/t/how-hard-was-the-learning-curve-for-you-guys/15698));
   "Context-sensitive build menu makes it difficult to know what to scavenge for"
   ([#4635](https://github.com/CleverRaven/Cataclysm-DDA/issues/4635)). The ImGui work exists
   because "navigation between elements and mouse-based selections involve code which currently
   needs to be repeated in every UI" ([#65709](https://github.com/CleverRaven/Cataclysm-DDA/pull/65709)).
   *3D:* out of scope; these stay overlays (as the first-person design already decided, F4).
10. **Accessibility is a hard constraint.** Blind players play CDDA through screen readers; "it's
    important this part of accessibility doesn't regress" (Zireael07 in
    [#65709](https://github.com/CleverRaven/Cataclysm-DDA/pull/65709)). Anything moved "into the
    world" must remain available as text.

### 1.4 Prior art inside the CDDA ecosystem

* **Sidebar presets and widgets.** See 1.1 and 1.2. The widget system already has clauses and
  conditions (colour and text by value ranges), graphs, and multi-line widgets (`doc/WIDGETS.md`),
  so a "3D" preset can be written in JSON without C++.
* **Structured sidebar** (mlange-42): separators, a compact environment/location block, a compact
  body graph including eyes and mouth, a compact overmap + compass "Everything at a glance" for
  travel and driving; merged into the game in 2022.
* **ZenFS thick sidebar**: built for ultra-wide monitors ("I've been using it all day on my ultra
  wide monitor and it's lovely", [#71109](https://github.com/CleverRaven/Cataclysm-DDA/pull/71109)).
  Discourse users name spacebar as easiest to read and structured for larger fonts (search
  summary, [What are your sidebar options?](https://discourse.cataclysmdda.org/t/what-are-your-sidebar-options/28203)).
* **Pixel minimap** (2019, [#28152](https://github.com/CleverRaven/Cataclysm-DDA/pull/28152)),
  which the first-person branch now rotates heading-up.
* **Tilesets** ([I-am-Erk/CDDA-Tilesets](https://github.com/I-am-Erk/CDDA-Tilesets)): UltiCa
  (default), Altica, MShockXotto+ (descended from MShock and Xotto), Chibi_Ultica, RetroDays and
  descendants; **isometric**: Ultica_iso, IsoMSX+, HitButton_iso ("Change the game to an isometric
  perspective"); **overmap tilesets**: Larwick_Overmap, ASCII_Overmap, SurveyorsMap, PenAndPaper.
  Isometric sets are the in-ecosystem precedent for "the same sprites seen from another angle". The
  overmap tilesets are ready-made art for horizon landmarks and POI icons.
* **3D attempts.** A very old "3D visualizer" drew the map as boxes in an SDL/OpenGL window on a
  fork's branch, not playable
  ([Discourse, The Lab](https://discourse.cataclysmdda.org/t/3d-visualizer/2215)); a 2019 plan for a
  MagicaVoxel tileset that would export to 3D, 2D and iso
  ([3d tileset plans](https://discourse.cataclysmdda.org/t/3d-tileset-plans/18424)) with no sign it
  was finished (both from search summaries). This fork's r3d is, as far as these searches found,
  the only playable 3D view.
* **Cataclysm: Bright Nights.** BN forked before the JSON widget system (its `data/json` has no
  `ui/` folder: [cataclysmbn/Cataclysm-BN](https://github.com/cataclysmbn/Cataclysm-BN/tree/main/data/json)),
  so its sidebar is still the older C++ panel set. It is described as having "a simpler UI" with
  less micromanagement (search summary of an [RPGHQ thread](https://rpghq.org/forums/viewtopic.php?t=165))
  and its 0.8.0 notes list small UI items (battery charge colours, armour totals, overmap search
  history) ([RogueBasin](https://www.roguebasin.com/index.php/Cataclysm:_Bright_Nights)). No Steam
  reviews of BN were found; BN does not seem to be on Steam.

### 1.5 What players say they want

* Keep everything visible; let me choose; do not hide things by context ("Please allow this to be
  an option", CoroNaut, [#27367](https://github.com/CleverRaven/Cataclysm-DDA/issues/27367)).
* Mode presets: exploration vs combat (KucharczykL, same thread); two switchable panel sets
  (nexusmrsep).
* **Enemy directions as markers at the edge of the map view instead of a sidebar compass**
  (Anrock, same thread). This is what the first-person edge alert does; orbit mode lacks it.
* Icons, pie charts and a paper doll instead of text (NotFuji, same thread); the body graph
  (`body_graph.json`) is the shipped answer.
* Visible cause for every interruption; warnings for long-range threats; z-level attack cues
  ([#42845](https://github.com/CleverRaven/Cataclysm-DDA/issues/42845),
  [#35378](https://github.com/CleverRaven/Cataclysm-DDA/issues/35378)).
* See the weather coming, with wind and temperature in the same view
  ([#27947](https://github.com/CleverRaven/Cataclysm-DDA/pull/27947)).
* A UI tutorial ([#57571](https://github.com/CleverRaven/Cataclysm-DDA/issues/57571)).
* Path previews for auto-travel ([#73062](https://github.com/CleverRaven/Cataclysm-DDA/issues/73062)).

---

## Part 2: how 3D games communicate state through the world

### 2.1 A vocabulary

Fagerholt and Lorentzon's thesis *Beyond the HUD* (Chalmers, 2009) sorts interface elements on
two axes, whether the element exists in the fiction and whether it sits in the 3D space:
**non-diegetic** (classic HUD: bars, text), **diegetic** (in the world and in the fiction: a watch,
a map the character holds), **spatial** (in 3D space but not in the fiction: a marker over a door,
an outline) and **meta** (in the fiction but not in space: blood on the screen, desaturation)
([thesis PDF](https://publications.lib.chalmers.se/records/fulltext/111921.pdf)). Below, each
technique is tagged with one of these.

### 2.2 Sky, time of day, weather, wind

| Game | Technique | Tag | Notes and criticism |
|---|---|---|---|
| Minecraft | Sun and moon cross the sky; the clock and compass are held items; exact time only in the F3 debug screen | diegetic | Players install clock mods or datapacks to see the time on the HUD ([Clockpass](https://www.modpackindex.com/mod/112209/clockpass), [VexClock](https://modrinth.com/mod/vexclock)): sky alone is not enough for planning |
| Valheim | No clock; sky and light only. Wind shown as an arrow under the minimap and, when sailing, against the ship icon | diegetic + HUD | Players build sundials and ask for clock mods ([TheGamer](https://www.thegamer.com/valheim-sundial/), [Handy Clock](https://thunderstore.io/c/valheim/p/Creaton/HandyClock/)); players asked for the wind arrow on the full map too ([sailing guide](https://www.gamepressure.com/valheim/sailing/zce2ad)) |
| The Long Dark | Minimal HUD; weather is the world (blizzards cut visibility, heard indoors); status on a key press | diegetic + on-demand HUD | "presents a lot of gameplay-relevant information through audio cues" ([PC Gamer](https://pcgamer.com/creating-a-killer-wilderness-in-the-long-dark/2), [wiki: Weather](https://thelongdark.fandom.com/wiki/Weather)) |
| Ghost of Tsushima | Guiding wind: grass, leaves and cloth blow towards the objective; no compass, no minimap | diegetic | Began as "make the whole world move"; mixed reception, some wanted a marker option, unclear in mountains ([Android Central](https://www.androidcentral.com/ghost-tsushima-creative-director-interview), [GDC 2021](https://gdconf.com/news/see-how-ghost-tsushima’s-guiding-wind-came-life-gdc-2021), [GodisaGeek](https://godisageek.com/reviews/ghost-of-tsushima-review)) |
| Breath of the Wild | Time, weather and temperature on the HUD; Pro HUD hides most of it | HUD, optional | The HUD toggle precedent ([Siliconera](https://www.siliconera.com/breath-wilds-pro-mode-disables-mini-map-temperature-meter-hud/), [Zelda Wiki](https://zeldawiki.wiki/wiki/Game_Screen)) |
| Project Zomboid | Simulated fronts, morning fog, visible rain/snow, fog masked from enclosed rooms; a wind indicator the devs were not happy with | diegetic + HUD | ([PZ blog](https://projectzomboid.com/blog/?p=6892), [GamingOnLinux](https://gamingonlinux.com/2018/10/project-zomboid-has-a-massive-update-with-a-simulated-weather-and-climate-system)) |
| Daggerfall Unity | Weather manager, sky, compass; procedural-sky mod popular | diegetic + HUD | ([DFTFU 1.2](https://www.dfworkshop.net/daggerfall-tools-for-unity-1-2/), [Nexus](https://www.nexusmods.com/daggerfallunity/mods/top?offset=-4)) |

Lesson: sky colour and light are good at "roughly what time is it" and bad at "how long until
dark"; wind is readable only when something moves with it; weather in the world is persuasive
(players act on it) but every game above keeps a text or icon fallback.

### 2.3 Points of interest and location

| Game | Technique | Tag | Notes and criticism |
|---|---|---|---|
| Skyrim / Oblivion | Compass tape with quest markers and discovered-location icons | spatial/HUD | "point A to point B"; players compare it badly with Morrowind's written directions ([GameBanshee](https://gamebanshee.com/kwyc9), [Steam](https://steamcommunity.com/app/22320/discussions/0/630800445044080818)) |
| Daggerfall Unity | Quest compass showing direction, name and distance to a target door; dungeon automap | spatial/HUD | Players still get lost in dungeons ([dfworkshop](https://www.dfworkshop.net/quest-buildings-and-pcat-condition/), [automap](https://forums.dfworkshop.net/viewtopic.php?p=1469)) |
| Outward | Paper maps with no "you are here", no minimap; landmarks do the work | diegetic | Praised ("useless map is its best feature") and disliked by confused players ([PC Gamer](https://pcgamer.com/outwards-useless-map-is-its-best-feature), [Steam](https://steamcommunity.com/app/794260/discussions/0/1768133742963231846/?ctp=5)) |
| Kingdom Come (hardcore) | Removes map markers from the compass | — | "Fifteen hours into Hardcore and I'm ready to get my map markers on the compass back" ([forum](https://forum.kingdomcomerpg.com/t/fifteen-hours-into-hardcore-and-im-ready-to-get-my-map-markers-on-the-compass-back/58956)) |
| Dread Delusion | No full map, a buyable compass, directions in dialogue and journal | diegetic | Reviewers liked it; also "barren at times" ([RPGamer](https://rpgamer.com/review/dread-delusion-review/), [CGM](https://www.cgmagonline.com/review/dread-delusion-pc-review/)) |
| Far Cry 2 | Character holds a paper map and GPS; zoom levels are sheets of paper | diegetic | Still needed a HUD for ammo and health ([Wikipedia](https://en.wikipedia.org/wiki/Far_Cry_2), [GDC 2010 slides](https://media.gdcvault.com/gdc2010/GNR/Alex%20Lifschitz_2010.pdf)) |
| Grimrock | Automap on by default; "old-school mode" turns it off | HUD | "At least there's an automap" ([Game Informer](https://gameinformer.com/games/legend_of_grimrock/b/pc/archive/2012/04/04/review)) |
| Ultima Underworld | Automap with player notes typed anywhere on it; compass in the panel | HUD | "near-miraculous" in 1992 ([Ultima Codex](https://gallery.ultimacodex.com/ultima-underworlds-auto-mapping-feature/), [refcard](https://mocagh.org/origin/uuw-refcard.pdf)) |
| Ghost of Tsushima | Wind (above); birds and smoke columns lead to points of interest | diegetic | Same sources as above |

Lesson: a compass tape is the accepted middle ground. Removing it is a niche taste; covering it
in markers is the common complaint. CDDA's overmap notes are already player-written labels, the
Ultima Underworld model.

### 2.4 Character state

| Game | Technique | Tag | Notes and criticism |
|---|---|---|---|
| Project Zomboid | Moodles: icons that appear only when a need or condition is active, stacked at the screen edge, with severity levels | HUD, contextual | Build 42 redraw criticised for style (clash with pixel art, "egg" injury icon, bleeding too understated); devs say it was for UI scaling ([Steam](https://steamcommunity.com/app/108600/discussions/0/595135660380156129), [pzfans](https://pzfans.com/moodle_madness_surviving_project_zomboids_status_secrets/)) |
| Valheim | Status icons with timers (Wet, Cold, Freezing, Resting, Rested); weather causes them | HUD | ([wiki: Wet](https://valheim.fandom.com/wiki/Wet), [status effects](https://gamever.io/knowledge-base/every-status-effect-in-valheim-with-the-numbers-behind-each-one)) |
| DayZ | Colour drains with blood loss, heartbeat, shaking, blur; shivering and teeth chattering for cold | meta + audio | Players note "not everyone plays with a HUD"; sources are old mod-era wikis ([wiki: Blood](https://dayz.wiki.gg/wiki/Mod:Blood), [Steam](https://steamcommunity.com/app/221100/discussions/0/591761634944382129)) |
| Breath of the Wild | Stamina wheel beside the character, shown only when used; temperature gauge; noise meter | HUD near the action | ([Outer Haven](https://www.theouterhaven.net/2016/06/hands-on-with-the-legend-of-zelda-breath-of-the-wild)) |
| Metro Exodus | Wrist watch: filter time, a light/stealth indicator; Geiger counter | diegetic | ([Analog Stick](https://www.analogstickgaming.com/xbox-one-reviews/2019/2/16/metro-exodus), [Digital Trends](https://qa2.www.digitaltrends.com/gaming/metro-exodus-beginners-guide-to-surviving)) |
| Cruelty Squad | Health as a pulsing coloured blob, deliberately unpleasant | HUD as art | "very unintuitive UI that might be uncomfortable to use but it works" ([VOICE](https://www.voicemag.uk/review/9524/cruelty-squad-an-audio-visual-nightmare), [Bit-Tech](https://bit-tech.net/reviews/cruelty-squad-review/1)) |
| Doom-likes (Doom, Hrot, Dusk) | Status bar with a face that bloodies with damage | HUD | ([ZDoom wiki](https://zdoom.org/wiki/Status_bar)); no Hrot or Dusk review could be read from this session |
| The Long Dark | Meters hidden until asked for; long actions shown as a filling circle | on-demand HUD | ([PC Gamer review](https://pcgamer.com/the-long-dark-review)) |

Lesson: the successful pattern is **contextual icons** (show only when abnormal, escalate by
colour) plus **a small number of strong meta effects** for the critical states (bleeding out,
freezing). Art style must match the game (the PZ complaint). Screen effects for everything turn the
picture into soup and cost readability; keep them for 2-3 states.

### 2.5 Threats

| Game | Technique | Tag | Notes and criticism |
|---|---|---|---|
| Metal Gear Solid V | Per-enemy awareness: white (noticed something) and red (spotted), with direction | spatial | ([guide](https://james.gamerguides.com/metal-gear-solid-v-the-phantom-pain/guide/the-basics-6/gameplay-9/stealth-and-combat), [MGI](https://www.metalgearinformer.com/metal-gear-solid-vs-marking-system-is-not-comparable-to-splinter-cells-mark-execute-kojima-productions-says/)) |
| Fortnite | "Visualize sound effects": coloured arcs around the reticle by direction and category (footsteps, gunfire, chests, vehicles) | HUD/spatial | Default on for all players; vertical direction is hard to read ([Can I Play That](https://caniplaythat.com/?p=14083), [GameSpot](https://www.gamespot.com/articles/how-to-use-visualize-audio-effects-in-fortnite/1100-6498314/)) |
| Minecraft | Subtitles; mods add direction arcs, damage direction and creeper warnings around the crosshair | HUD | ([EchoSight](https://modrinth.com/mod/echosight), [Accessibility Alerts](https://www.curseforge.com/minecraft/mc-mods/accessibility-alerts), [Subtitles Visualizer](https://www.curseforge.com/minecraft/mc-mods/pepsied-subtitles-visualizer)) |
| Skyrim | Combat music starts when an enemy targets you | audio | "often the first sign that you have been targeted by an unseen enemy ... conferring an unfair advantage" ([Nexus mod](https://www.nexusmods.com/skyrim/mods/20926)) |
| Breath of the Wild | Noise meter shows how loud you are | HUD | (above) |
| Metro | Watch light shows whether you can be seen | diegetic | (above) |

Lesson: direction-of-threat arcs around the centre are now standard, and are an accessibility
feature, not only a convenience. Music or effects driven by hidden state leak information the
player should not have; in CDDA, only show what `you.sees()` or the sound system already tells the
player.

### 2.6 What goes wrong, in short

* Marker-following replaces looking (Skyrim).
* Removing navigation aids loses most players (Outward split, KCD hardcore, Grimrock old-school is
  opt-in).
* Diegetic devices that are slower than a number get worked around (Far Cry 2's HUD fallback,
  Minecraft and Valheim clock mods).
* State leaks through presentation (Skyrim music).
* Restyled icons that clash with the art (PZ moodles).
* Wind as the only guide fails on steep terrain (Tsushima review).
* Too many screen effects; motion effects cause nausea (the first-person design already bans
  head-bob, `R3D_FIRST_PERSON.md` C1).

---

## Part 3: synthesis for this project

### 3.1 What the renderer has and lacks

Has (from `doc/R3D_STATUS.md`, `R3D_FIRST_PERSON.md` and the code on both branches):

* Per-vertex light from the visibility cache; remembered tiles sepia; unseen tiles black and, in
  first person, depth-writing.
* Sky colour by time of day, **first person only**: a lerp between a night and a day colour by
  `sun_moon_light_at() / default_daylight_level()` (`src/r3d.cpp`, background pass). Orbit mode
  clears to a fixed near-black.
* Orbit cutaway of walls and roofs.
* First person: compass strip with N/E/S/W, red hostile ticks, yellow sound ticks; edge alert when
  an adjacent hostile is out of view; crosshair with a label naming the tile; sound markers as
  posts on their tile; "Looking: N level(s) up/down"; heading-up pixel minimap (`src/r3d_hud.{h,cpp}`).
* `r3d::hud_texts()` drawn in the game font by `sdltiles.cpp`.

Lacks:

* **Everything `tilecontext->draw` overlays in 2D**: weather animation (`draw_weather_frame`),
  scrolling combat text (`draw_sct_frame`), zones (`draw_zones_frame`), and in orbit mode the
  footstep markers (`draw_footsteps_frame`), because the `drawn_3d` branch skips that call.
* Weather in 3D (particles, fog, cloud-darkened sky, lightning), wind.
* Sun and moon in the sky; moon phase.
* Any HUD in orbit mode (no compass, no off-screen threat pointers).
* POI pointers: mission target (`avatar::get_active_mission_target`), auto-travel destination
  (`game::destination_preview`, `Character::omt_path`), overmap notes
  (`overmapbuffer::get_all_notes`). Not done in first person either (the auto-travel caret is listed
  as not done).
* Anything beyond the reality bubble (no horizon, no landmarks).
* Damage direction, floating damage numbers.
* Vehicles (first-person F5).

### 3.2 Principles

1. **Redundant, never sole.** Every in-world cue duplicates something the sidebar or log already
   says (screen readers, [#65709](https://github.com/CleverRaven/Cataclysm-DDA/pull/65709)), and
   veterans want the full sidebar ([#27367](https://github.com/CleverRaven/Cataclysm-DDA/issues/27367)).
2. **Only what the character perceives.** Drive cues from the same sources as the text:
   `you.sees()`, the sound markers, `get_weather()`, the clock/thermometer gating. No music or
   effect from hidden state (Skyrim lesson).
3. **Contextual HUD, permanent sidebar.** The overlay on the 3D picture shows a thing only when it
   is abnormal or changing; the sidebar shows everything all the time.
4. **The world carries the ambient, the HUD carries the actionable.** Time, weather, light, wind,
   cold: world. Numbers you act on in a fight (HP per part, stamina, safe mode): HUD.
5. **Same cue in orbit and first person** where possible, so a player switching with ctrl+F
   keeps the same vocabulary.

### 3.3 Mapping table

Columns: **Keep** = keep as sidebar/HUD text (unchanged); **Glyph** = icon or bar on the 3D overlay;
**World** = drawn in the scene; **Drop** = leave off the 3D overlay (still in the sidebar). Orbit and
first person (FP) noted when they differ.

| Information | CDDA source | Keep | Glyph (3D overlay) | World | Notes |
|---|---|---|---|---|---|
| Body part HP | `bp_hp`, `body_graph.json` | yes | small body silhouette, tinted part flashes when hit (FP and orbit) | orbit: red flash on the avatar sprite when hit; FP: red screen-edge flash from the hit's direction | Paper doll is what players asked for ([#27367](https://github.com/CleverRaven/Cataclysm-DDA/issues/27367)) |
| Bleeding / broken / bitten | `bodypart_status.json` | yes | moodle icon while active | FP: slow desaturation while bleeding badly (DayZ) | One of 2-3 allowed meta effects |
| Stamina | `stamina` | yes | bar near the crosshair (FP) or under the avatar (orbit) only when below full (BotW wheel) | — | Most-read number in a fight |
| Mood / morale | `mood_text`, `morale_level` | yes | — | — | Drop from overlay |
| Focus | `focus` | yes | — | — | Matters in crafting/reading only |
| Speed | `speed` | yes | — | — | Drop |
| Moves | `move`, `move_remainder` | yes | — | — | Drop |
| Move mode | `move_mode_letter` | yes | — | avatar posture (crouch/prone sprite, orbit); FP: lower eye height when crouched/prone | Eye height is already an option in FP |
| Safe mode | `safe_mode_text` | yes | shield glyph on the compass strip / orbit corner; when it stops you, pulse it and mark the cause on the compass | the cause highlighted in the scene | Answers "why did I stop?" ([#57571](https://github.com/CleverRaven/Cataclysm-DDA/issues/57571)) |
| Weariness / activity | `weariness_text`, `activity_text` | yes | moodle at "Weary" or worse | — | |
| Pain | `pain_text` | yes | moodle at "Distracting" or worse | optional faint pulse at "Intense"+ (option, off by default) | |
| Hunger / thirst / sleepiness | `hunger.json`, `thirst.json`, `sleepiness.json` | yes | moodle only when not normal | sleepiness "Exhausted": slow vignette (option) | PZ moodle model |
| Body temperature | `body_temp.json`, `bp_warmth` | yes | moodle (cold/hot) | breath vapour puffs from creatures and the avatar in cold air; FP: frost at the screen corners at "Freezing" | |
| Wetness | `bp_wetness` | yes | moodle | wet sheen on the avatar (orbit) | |
| Air temperature | `env_temp_text` (needs thermometer) | yes | — | snow cover, frost, heat shimmer by weather type | Keep the thermometer gating for the number |
| Weather | `weather_text`, `weather_type.json` | yes | — | rain/snow/portal-storm particles, fog distance, sky darkened by cloud, lightning flash, ground darkened when wet | Biggest missing piece |
| Wind | `wind_text`, `weather_manager::windspeed/winddirection` | yes | FP: wind arrow on the compass strip (Valheim) | particles drift with the wind; trees and grass lean | Tsushima: wind is readable only when things move |
| Moon | `moon.json` | yes | — | FP: moon disc in the sky with its phase; nights brighter at full moon (light already does this) | |
| Date / time | `date_text`, `time_text` | yes | — | sky colour (FP has it; add to orbit as clear colour); sun and moon position (`sundial_text` already computes it) | Sky tells "roughly", sidebar tells exact |
| Place | `place_text`, `overmap_loc_text` | yes | toast "Entering: <place>" on `avatar_enters_omt` | signs, horizon landmarks | |
| Lighting | `lighting_desc` | yes | — | the picture is the lighting | Drop from overlay |
| Hostile compass | `compass_text`, `compass_legend_text` | yes | FP: compass strip (done); orbit: arrows at the window edge pointing at off-screen hostiles (Anrock's idea) | creatures themselves | |
| Heard sounds | sound markers (`sounds::get_footstep_markers`) | log | compass ticks (FP done; add orbit) | posts on their tile (FP done; add orbit) | Fortnite/Minecraft accessibility model |
| Own noise | `sound` var | yes | small noise meter (BotW) | ring on the floor around the avatar sized by noise (orbit) | |
| Messages | log | yes | — | floating damage text and "miss" over the target (port `draw_sct_frame`) | Moves the combat half of the log into the world |
| Overmap / destination | overmap, `omt_path`, missions, notes | yes | carets on the compass strip: mission target, travel destination, nearest notes | path ribbon on the ground for auto-travel; landmarks beyond the bubble | [#73062](https://github.com/CleverRaven/Cataclysm-DDA/issues/73062) path overlays |
| Z-level threats | `you.sees()` across levels | log | compass tick with up/down mark | attacker drawn above/below (orbit shows it; FP needs the tilt) | [#42845](https://github.com/CleverRaven/Cataclysm-DDA/issues/42845) |
| Turret / ranged warnings | monster special attacks | log | red tick | laser line / red dot on the avatar when targeted | [#35378](https://github.com/CleverRaven/Cataclysm-DDA/issues/35378) |
| Vehicle azimuth / cruise / fuel | `veh_*_text` | yes | small dashboard when driving | the vehicle (first-person F5) | |
| Radiation, power, mana, oxygen | badges and numbers | yes | moodle when abnormal (low oxygen, high rads) | — | |
| Wielded item | `wielding_text` | yes | — | FP: optional held-item sprite at the bottom of the view (later) | |

### 3.4 Orbit view: recommendations

* **Restore parity with 2D first**: weather animation, SCT and footstep markers (they work in 2D
  tiles today; in 3D they vanish).
* **Sky as the clear colour in orbit too**, using the same lerp as first person. It is one line of
  logic moved out of `if( fp )` and immediately shows time of day around the edges of the map.
* **Weather particles** in the scene (camera-relative volume of billboards, falling, drifting with
  the wind; snow slower), plus distance fog by weather (`weather_type` already has a sight
  penalty) and a cloud-darkened sky. The tileset's `weather_rain_drop` / `weather_snowflake`
  sprites can be the particle sprites, so every tileset works.
* **Off-screen hostile arrows** at the edge of the terrain window, reusing the bearing maths in
  `r3d_hud.cpp`.
* **A floor ring for player noise** and footstep posts for heard sounds.
* **POI ribbon** on the ground for auto-travel and look-mode paths (`destination_preview`).

### 3.5 First person: recommendations

* Keep the compass strip as the one place for direction: add **wind arrow**, **mission target**,
  **travel destination**, **overmap notes within N tiles**, and up/down marks on hostile ticks.
* **Sun and moon discs** in the sky (position from the same calculation as `sundial_text`; phase
  from `get_moon_phase`), **stars** on clear nights, **cloud cover** darkening the gradient.
* **Weather particles and fog** (shared with orbit).
* **Hit direction**: a red arc at the screen edge towards the attacker, from `character_takes_damage`
  on the event bus (`src/event.h`), lasting one redraw cycle; extends the existing edge alert.
* **Contextual moodle row** under the compass strip (icons for needs, temperature, pain, bleeding,
  safe mode), drawn from the same widget clauses as the sidebar so thresholds stay in one place.
* **Floating damage text** over billboards.
* **Horizon landmarks** (later): beyond the reality bubble, draw overmap tiles of known places as
  flat impostors at their bearing, using an overmap tileset's sprites; smoke columns over known
  fires; lit windows at night in seen towns.
* Keep all of it off by option where it is an effect (vignettes, frost, pulses), on by default
  where it is information (compass marks, weather).

### 3.6 Ten changes worth doing first

Ordered by value for effort. Effort: S = a day or two, M = about a week, L = several weeks.

| # | Change | Why | Effort | Code area |
|---|---|---|---|---|
| 1 | **Weather in 3D**: rain/snow/portal-storm particles using the tileset's weather sprites, drifting with the wind; fog distance by weather; sky darkened by cloud | Biggest gap; the 3D view currently shows no weather at all, the 2D one does | M | `src/r3d.cpp`, `src/r3d_entities.cpp` (particle billboards), read-only from `src/weather.cpp` / `weather.h` (`get_weather()`, `windspeed`, `winddirection`, `weather_type::tiles_animation`) |
| 2 | **Put back the 2D overlays the 3D path skips**: scrolling combat text, footstep markers in orbit, zone highlights | Parity; SCT moves combat out of the log | M | `src/sdltiles.cpp` (`drawn_3d` branch), `src/cata_tiles.cpp` (`draw_sct_frame`, `draw_footsteps_frame`), `src/r3d.cpp` (project tile to screen) |
| 3 | **Sky in orbit, sun/moon/stars in first person** | Time of day at a glance in both modes; reuses existing maths | S (orbit) + M (discs) | `src/r3d.cpp` background pass; sun position as in `widget.cpp` `sundial_text`; `calendar.cpp` moon phase |
| 4 | **Orbit HUD**: off-screen hostile and sound arrows at the window edge | Asked for in #27367; FP has it, orbit has nothing | S-M | `src/r3d_hud.{h,cpp}` (bearings exist), `src/r3d.cpp` hud texts |
| 5 | **Compass strip carets**: wind arrow, mission target, auto-travel destination, overmap notes, up/down marks on threat ticks | Joins overmap and local map; z-level threats (#42845) | M | `src/r3d_hud.cpp`, `src/r3d.cpp`; data from `avatar::get_active_mission_target`, `Character::omt_path`, `overmapbuffer::get_all_notes` (`src/overmapbuffer.h`), `src/overmap_ui.cpp` for note symbols |
| 6 | **A 3D sidebar preset** (`sidebar-r3d.json`): drops lighting, and moves weather/wind/moon/time into one compact line; keeps HP, stamina, needs, safe mode, log, minimap; frees rows for the log | Cheapest way to give the 3D view more room without hiding anything | S | `data/json/ui/` only: a new `"style": "sidebar"` widget reusing existing widgets; `src/panels.cpp` `initialize_default_panel_layouts` already adds a layout for every sidebar-style widget |
| 7 | **Contextual moodle row** on the 3D overlay, driven by widget clauses (show only when the clause is not the "normal" one) | PZ / BotW pattern; status without reading the sidebar | M | `src/widget.cpp` (expose a "current clause severity" for a widget), `src/r3d_hud.cpp` / `src/r3d.cpp` (draw), new small JSON list of which widgets become moodles in `data/json/ui/` |
| 8 | **Hit direction and damage flash** (edge arc in FP, sprite flash in orbit) | Direction of the attacker is the most-asked threat cue in 3D games | S-M | `src/r3d_hud.cpp`, subscribe to `event_type::character_takes_damage` (`src/event.h`, `src/event_bus.h`) |
| 9 | **"Entering <place>" toast and travel ribbon**: on `avatar_enters_omt` show the place name over the view for a few seconds; draw `destination_preview` as a ground ribbon | Sense of place; auto-travel friction (#73062) | S | `src/r3d.cpp`, `src/game.h` (`destination_preview`), event bus |
| 10 | **Horizon landmarks**: known overmap specials beyond the bubble drawn as distant impostors from an overmap tileset; smoke over known fires | Finding POIs; Outward/Tsushima lesson that landmarks navigate | L | `src/r3d_mesh.cpp` or a new `src/r3d_horizon.cpp`, `src/overmapbuffer.h` (seen/explored tiles), `src/overmap_ui.cpp` (symbols) |

Also worth doing later: breath vapour and posture (S each, `src/r3d_entities.cpp`); laser line
from targeting turrets (S once the targeting state is exposed); held-item sprite in first person
(M); a "Look at the sky" view that renders the weather map as the actual sky overhead (M,
`src/overmap_ui.cpp` + `src/r3d.cpp`).

---

## Open questions for the users

1. **How much should go into the world vs stay text?** Is the goal "fewer glances at the sidebar"
   or "a sidebar you can switch off"? The second needs much more (HP, needs, log), and goes against
   the accessibility point.
2. **Effects: yes or no?** Screen-edge frost, desaturation when bleeding, vignette when exhausted:
   on by default, off by default, or not at all? (Motion sickness is already handled; these are
   about readability and taste.)
3. **Markers or landmarks?** Should the compass show the mission target and travel destination
   (Skyrim/Daggerfall style), or should finding places stay a map-reading task (Outward style)? A
   middle path is "only things you put there yourself": notes and the auto-travel destination.
4. **Weather strength.** Should rain and fog actually make the 3D picture harder to read (honest to
   the rules: weather already reduces sight), or stay light so the picture stays readable?
5. **Sound.** CDDA has soundpacks but no positional audio. Is visual sound (markers, compass ticks,
   noise ring) enough, or is positional audio wanted later?
6. **Which sidebar do you play with now?** A 3D preset should start from the one you like. Do you
   want a wider log in exchange for dropping lighting/moon/focus/moves from the sidebar?
7. **Orbit vs first person priority.** Which mode do you expect to play in most? Several items help
   both; items 4 and 8 differ.
8. **Tileset.** Which tileset do you use? Weather particles and moodle icons should come from it
   (or be drawn to match it, the PZ lesson about clashing art).
9. **Player opinion we could not read.** Reddit, Steam reviews, the Discourse forum and YouTube
   could not be opened from this session. If you have threads or videos you agree with, point at
   them and this note can be extended.

---

## Sources

Read directly (GitHub, repo):

* CleverRaven/Cataclysm-DDA issues and PRs: [#2633](https://github.com/CleverRaven/Cataclysm-DDA/issues/2633),
  [#4635](https://github.com/CleverRaven/Cataclysm-DDA/issues/4635),
  [#8300](https://github.com/CleverRaven/Cataclysm-DDA/issues/8300),
  [#10637](https://github.com/CleverRaven/Cataclysm-DDA/issues/10637),
  [#27367](https://github.com/CleverRaven/Cataclysm-DDA/issues/27367),
  [#27439](https://github.com/CleverRaven/Cataclysm-DDA/pull/27439),
  [#27743](https://github.com/CleverRaven/Cataclysm-DDA/pull/27743),
  [#27947](https://github.com/CleverRaven/Cataclysm-DDA/pull/27947),
  [#28152](https://github.com/CleverRaven/Cataclysm-DDA/pull/28152),
  [#28215](https://github.com/CleverRaven/Cataclysm-DDA/issues/28215),
  [#28672](https://github.com/CleverRaven/Cataclysm-DDA/issues/28672),
  [#34621](https://github.com/CleverRaven/Cataclysm-DDA/issues/34621),
  [#35378](https://github.com/CleverRaven/Cataclysm-DDA/issues/35378),
  [#42845](https://github.com/CleverRaven/Cataclysm-DDA/issues/42845),
  [#51438](https://github.com/CleverRaven/Cataclysm-DDA/pull/51438),
  [#57571](https://github.com/CleverRaven/Cataclysm-DDA/issues/57571),
  [#58113](https://github.com/CleverRaven/Cataclysm-DDA/pull/58113),
  [#63502](https://github.com/CleverRaven/Cataclysm-DDA/pull/63502),
  [#65709](https://github.com/CleverRaven/Cataclysm-DDA/pull/65709),
  [#71109](https://github.com/CleverRaven/Cataclysm-DDA/pull/71109),
  [#73062](https://github.com/CleverRaven/Cataclysm-DDA/issues/73062),
  [#73134](https://github.com/CleverRaven/Cataclysm-DDA/pull/73134),
  [#73456](https://github.com/CleverRaven/Cataclysm-DDA/pull/73456),
  [#78465](https://github.com/CleverRaven/Cataclysm-DDA/issues/78465),
  [#78557](https://github.com/CleverRaven/Cataclysm-DDA/pull/78557);
  UI-labelled list sorted by comments:
  [search](https://github.com/CleverRaven/Cataclysm-DDA/issues?q=sidebar+label%3A%22Info+%2F+User+Interface%22+sort%3Acomments-desc).
* [mlange-42/cdda-structured-sidebar](https://github.com/mlange-42/cdda-structured-sidebar);
  [I-am-Erk/CDDA-Tilesets](https://github.com/I-am-Erk/CDDA-Tilesets);
  [cataclysmbn/Cataclysm-BN data/json](https://github.com/cataclysmbn/Cataclysm-BN/tree/main/data/json).
* This repo: `doc/WIDGETS.md`, `doc/R3D_STATUS.md`, `data/json/ui/*.json`,
  `data/json/weather_type.json`, `data/raw/keybindings.json`, `src/panels.cpp`, `src/widget.cpp`,
  `src/weather.h`, `src/event.h`, `src/game.h`, `src/avatar.h`, `src/character.h`,
  `src/overmapbuffer.h`, `src/cata_tiles.h`; on `origin/feature/r3d-first-person`:
  `doc/R3D_FIRST_PERSON.md`, `src/r3d.cpp`, `src/r3d_hud.h`, `src/sdltiles.cpp`.

Known through search-engine summaries only (pages could not be opened from this session):

* CDDA community: [Discourse: What are your sidebar options?](https://discourse.cataclysmdda.org/t/what-are-your-sidebar-options/28203),
  [Ya'll messed up the interface](https://discourse.cataclysmdda.org/t/yall-messed-up-the-interface/19310),
  [How hard was the learning curve](https://discourse.cataclysmdda.org/t/how-hard-was-the-learning-curve-for-you-guys/15698),
  [Good font for overmap?](https://discourse.cataclysmdda.org/t/good-font-for-overmap/15215),
  [Overmaps, GPS, Maps, Sight](https://discourse.cataclysmdda.org/t/overmaps-gps-maps-sight-and-positioning/12940),
  [3D visualizer](https://discourse.cataclysmdda.org/t/3d-visualizer/2215),
  [3d tileset plans](https://discourse.cataclysmdda.org/t/3d-tileset-plans/18424),
  [The tutorial is not at all user-friendly](https://discourse.cataclysmdda.org/t/the-tutorial-is-not-at-all-user-friendly-for-newbies/4294);
  [Steam store page](https://store.steampowered.com/app/2330750/Cataclysm_Dark_Days_Ahead/);
  [Steam tutorial guide](https://steamcommunity.com/sharedfiles/filedetails/?id=2955072583);
  [TouchArcade review (2016)](https://toucharcade.com/2016/01/15/cataclysm-dark-days-ahead-review/);
  [GamingOnLinux (2019)](https://www.gamingonlinux.com/2019/03/cataclysm-dark-days-ahead-a-free-and-open-source-turn-based-survival-game-had-a-huge-update/page=1/);
  [RogueBasin: Bright Nights](https://www.roguebasin.com/index.php/Cataclysm:_Bright_Nights);
  [RPGHQ: Bright Nights](https://rpghq.org/forums/viewtopic.php?t=165).
* Design framework: [Fagerholt & Lorentzon, Beyond the HUD (2009)](https://publications.lib.chalmers.se/records/fulltext/111921.pdf).
* Games: all links in the Part 2 tables (Minecraft, Valheim, The Long Dark, Ghost of Tsushima,
  Breath of the Wild, Project Zomboid, Daggerfall Unity, Skyrim/Morrowind, Outward, Kingdom Come,
  Dread Delusion, Far Cry 2, Grimrock, Ultima Underworld, DayZ, Metro Exodus, Cruelty Squad,
  ZDoom, Metal Gear Solid V, Fortnite, Minecraft accessibility mods, Kenshi below).
* Kenshi (not used in the tables; UI judged "dated but useful" by critics and odd-coloured by
  modders): [OpenCritic](https://opencritic.com/game/7007/-),
  [Steam workshop thread](https://steamcommunity.com/workshop/filedetails/discussion/2804082382/3280321473020126088).
* Caves of Qud's modern UI (accessible, fully mouse- and gamepad-driven, classic text UI kept as an
  option): [Fullcleared](https://fullcleared.com/?p=16032),
  [1.0 devlog](https://freeholdgames.itch.io/cavesofqud/devlog/845582/caves-of-qud-10-out-now).
* Dwarf Fortress Adventure mode (Steam, January 2025, with a tutorial and improved UI):
  [mein-mmo](https://mein-mmo.de/dwarf-fortress-steam-adventure-mode-rollenspiel-statt-simulation/),
  [Bay 12](https://bay12games.com/dwarves), [Steam Deck HQ](https://steamdeckhq.com/news/dwarf-fortress-adventure-mode-released/).

Gaps: no usable source was found for UnReal World's presentation, Arx Fatalis, Dusk or Hrot, so
they are not used; YouTube reviews of CDDA were not reachable.
