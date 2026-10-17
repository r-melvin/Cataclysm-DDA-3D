# Onboarding review and "baby mode" design

## Status (implementation on `feature/baby-mode`)

Done:

- **M1**: guided hints for the first ten minutes.
  - Option **Settings > Interface > Guided hints** (`ONBOARDING_HINTS`): *Off*, *Play Now
    characters* (default: on for characters made with either Play Now entry, off for others),
    *Hints only* (every character, no popups), *Hints and popups* (every character). Off means
    nothing at all happens: no messages, no sidebar line, no popups. The Tutorial Game is never
    touched (hints are off in its world).
  - JSON type `onboarding_lesson` in `data/json/onboarding/lessons.json`: welcome, walk, let time
    pass, turn the camera (3D orbit only), first-person view (first person only), read the sidebar,
    look, use furniture, pick up, inventory, wear/wield, carry more, doors, safe mode, first
    monster, and a status-report lesson that only appears in builds that have that action.
  - Rules in `src/onboarding_core.{h,cpp}` (no game types, unit tested in
    `tests/onboarding_test.cpp`); game side in `src/onboarding.{h,cpp}`: event-bus subscriber,
    a check before every player input (polls and popups), an action hook next to
    `gamemode->pre_action`, the safe-mode "Spotted" hook, saving with the avatar (`"onboarding"`
    member), and loading/checking of the JSON (`--check-mods` reports bad actions, events, states,
    signals, UI flags and prerequisites).
  - Hints go to the message log as `[Hint: title] text` (re-read with `P`), and the sidebar shows
    a **Next step** panel (widget `onboarding_next`, var `onboarding_text`, in
    `data/json/ui/onboarding.json`): `Next: <title> (n/total)` plus the one-line instruction with
    the live key. It takes no space when there is nothing to show. It is listed first in every
    core sidebar preset.
  - Lessons complete themselves when the player does the thing; a lesson done before it was due
    is completed quietly and never shown. At most three hints are active at once; each reminds at
    most twice (every `snooze_turns`).
  - Popups (welcome, safe mode, first monster) offer **Got it** (`g`/Enter/Esc), **Remind me
    later** (`l`) and **Stop hints** (`S`, sets the option to Off).

- **M2**: the first hour, the checklist and "explain this screen".
  - Phase 2 lessons in `data/json/onboarding/lessons_phase2.json`: hunger, thirst and water,
    crafting and its search, time passing and waiting, darkness and night, sleep, the map,
    listing what is around (`V`), reading, the character sheet, low mood, getting hurt, bleeding
    and bites, and a death popup shown before the death screens.
  - **F1** (`onboarding_checklist`): the checklist of every lesson that applies to this game,
    grouped (first steps / the first hour / the first days), with done `[x]`, current `[>]` and
    pending `[ ]` marks and each lesson's one-line instruction. Pick a lesson to read its full
    text, show it again, or mark it done.
  - **F2** (`EXPLAIN_SCREEN`): explains the screen you are on. Texts are JSON
    (`onboarding_screen`, `data/json/onboarding/screens.json`, one per input-context category):
    main screen (with 3D variants), item lists, crafting, advanced inventory, aiming, map, look
    around, character sheet, message log, construction, clothing layers, generic menus. It works
    on every screen through one hook in `input_context::handle_input`, and only when the screen
    does not use F2 itself (the trade screen does) and hints are on.
  - Both keys can be rebound in the keybindings menu like any other.

How to try it: build, start the game, **New Game > Play Now! (Default Scenario)**. A welcome popup
appears on the first turn. Press Enter; the sidebar then reads `Next: Walk around (0/12)` and the
log has the hint. Walk five steps, press `.`, look with `;`, stand next to a locker and press `e`,
pick something up with `g`, open `i`. Lessons tick off in the log (`[Hint] Done: ...`). To see the
hints on an existing save or a custom character, set the option to *Hints and popups*.  Press F1
at any time for the checklist and F2 on any screen (inventory, crafting, map...) for an
explanation of it.

Works with the 3D UI: nothing is tied to a screen position. The Next-step line is an ordinary
widget, so any sidebar preset (including a lean 3D preset) can list it, and its text is always
in the message log as well. Lessons branch at runtime on what is active, through UI flags
(`classic`, `r3d`, `r3d_orbit`, `r3d_first_person`, `r3d_wasd`, `r3d_cutaway`, `r3d_compass`,
`r3d_edge_alert`) computed from `RENDERER_3D` and the `R3D_*` options, where a missing option counts
as off (so the same code runs on branches without the first-person options). A lesson can be
limited to a UI (`"only_when"`), and can carry `"variants"` that replace its text or add an
`"extra"` paragraph for a UI: walking explains W A S D and arrow-key turning when `R3D_WASD` is
on, first person explains turning and the face-threat key, the sidebar lesson mentions the
first-person compass strip and edge alert when those options are on, and the camera lesson only
exists in orbit view. Key names always come from the input manager (`<keybind:...>`), and with
`R3D_WASD` on, a key that is one of w a s d is replaced by "the action menu", because those keys
walk instead. A lesson can name `"requires_action"` (used for the planned `status_report` action
of the UI overhaul); it is skipped while that action does not exist or has no key.

Not done yet: see the M3 list in section 4.

---

Original status: design document only. Branch `feature/r3d-renderer`, repo head `3e0db5ee`
at the time of writing. Everything here is independent of the 3D renderer and the first-person mode;
the one place they interact is noted in section 5.

Audience: two players who have never played Cataclysm: Dark Days Ahead (CDDA) and want the game
itself to teach them, plus whoever implements the result.

Contents

1. Audit of what the game ships today
2. What a new player must learn, in order
3. Design: baby mode
4. Milestones and estimates
5. Open questions
6. Sources

---

## 1. Audit: what the game ships today

### 1.1 Summary table

| Piece | Where | What it teaches | Main problems |
| --- | --- | --- | --- |
| Tutorial Game (main menu) | `src/gamemode_tutorial.cpp/.h`, `src/gamemode.cpp`, `src/main_menu.cpp:842-877`, `data/mods/dda_tutorial/*` | Movement, look, movement modes, doors/windows/smash, pickup, wield, wear, container inventory, safe mode (one line), melee vs a dummy, reach attack, holsters, gun load/fire/recoil, flashlight + battery reload, stairs and darkness, water dispenser, remote-use lantern, hunger -> crafting a sandwich -> eating, construction (painting), pain -> codeine, traps (bubble wrap), throwing, action menu and keybinding menu | Modal popup per lesson, nothing persists, no re-read, no checklist; ~25 min linear corridor; skills forced to 5 so combat feels nothing like a real game; no overmap, sleep, reading, morale, zones, advanced inventory, vehicles, NPCs, weather, death; several lessons in the C++ enum are dead; the final screen recommends a scenario, then drops the player into the real game with zero support |
| Help (`H<e>lp` in main menu, `help` action in game) | `src/help.cpp` (ImGui window), `data/core/help.json` (23 categories, about 64 kB of text), mods add their own via `help_files.json` | Reference prose on movement, needs, pain, healing, morale, crafting, items, combat, vehicles, skills, FAQ | Reference, not instruction. Dense paragraphs; no pictures of the screens it talks about; no link from a confusing screen to the paragraph explaining it. Default in-game key is `0`/keypad 0 (`data/raw/keybindings.json` ~3398), which nobody discovers; `?` opens the keybinding list instead (`HELP_KEYBINDINGS`), which is what every guide tells you to press |
| Keybindings menu | `?` anywhere (`input_context::display_menu`) | Which keys exist in the current screen | The best discovery tool the game has, but it is a flat list of 100+ actions in the default mode with no grouping by "what you will need first" |
| New Game flow | `src/main_menu.cpp:475-482` (Custom / Preset / Random / Play Now (default scenario) / Play Now), `src/newcharacter.cpp` (`Character::randomize`, `avatar::create`) | Nothing. "Play Now! (Default Scenario)" gives a random character in the Evacuee scenario (`data/json/scenarios.json`, `sloc_shelter_safe`) | New players are told by the tutorial finale and by every guide to use it, but no text explains the difference between the five entries, or that the character screen's point budget can be ignored |
| World creation | `src/worldfactory.cpp` | Nothing | Not covered here; Play Now skips it, which is correct |
| MOTD | `data/motd/en.motd`, shown by `main_menu_opts::MOTD` | Links, experimental-build disclaimer | Not onboarding; fine as is |
| First-day messages | `game::start_game` (`src/game.cpp:790-1075`) | Nothing. It fires `game_start`, `game_avatar_new`, `avatar_enters_omt` events and shows "Please wait as we build your world" | There is no welcome, no "you are in an evacuation shelter, here is what to do first", no pointer to help. The only early guidance is the safe-mode warning string `"Spotted %1$s -- safe mode is on!  (%2$s to turn it off, %3$s to ignore monster%4$s)"` (`src/game.cpp:7412`) |
| Tip of the day / hints | none | n/a | Searched `src/` and `data/` for tip/hint systems: there is no loading-screen tip, no contextual hint system, no "learning helper". `uistate.h` and `options.cpp` carry no tutorial/hint flags |
| Scenario / profession descriptions | `data/json/scenarios.json`, `data/json/professions.json` | Flavour and a sentence of situation | Good flavour, but nothing says "Evacuee is the beginner one" except the tutorial finale's last paragraph |
| Achievements | `data/json/achievements.json`, `src/achievement.*`, `src/event_statistics.*` | Nothing; milestones after the fact | Infrastructure (event-driven, persistent, per-world) is exactly what a checklist needs, see 3.6 |

### 1.2 The Tutorial Game in detail

How it is wired:

- `main_menu.cpp:842-877`: creates a world named `TUTORIAL` with mods `dda` + `dda_tutorial`
  (`data/mods/dda_tutorial/modinfo.json` is flagged `"obsolete": true` only so it is hidden from
  the mod picker), sets `g->gamemode = tutorial_game`, calls `init()`.
- `tutorial_game::init()` (`gamemode_tutorial.cpp:144-200`): places the `tutorial` overmap special
  at a fixed overmap position, strips monster groups, resets all needs, forces 20 C, names the
  character John Smith, dresses them, and sets gun/melee/throwing skill to 5.
- `tutorial_game::per_turn()` (called from `src/do_turn.cpp:548`) scans the 3x3 around the player for
  doors/windows/racks/stairs/dispenser and fires a lesson; also hunger/pain injection via
  `tr_tutorial_11` and `tr_tutorial_13` pressure plates.
- `pre_action`/`post_action` (called from `src/handle_action.cpp:3403/3425`) react to OPEN, CLOSE,
  FIRE, USE, EAT, WEAR, PICKUP, SAVE.
- `add_message()` looks up a snippet in `data/mods/dda_tutorial/snippets.json` by the enum name and
  shows it with `popup(text, PF_ON_TOP)`. Seen-state lives in a `std::map` on the gamemode object and
  is not saved.
- The newer half of the tutorial is data: invisible `tr_tutorial_N` traps (`traps.json`) run
  `EOC_LESSON_*` effect-on-conditions whose `u_message` has `"popup": true`; a `u_seen_LESSON_X`
  math variable keeps them one-shot. The map is one 24x24 building with a basement
  (`mapgen.json`, `overmap_terrain.json`, `specials.json`) and one monster, `mon_dummy_tutorial`.

What it covers, in corridor order: intro and keybinding menu; movement keys; mouse look / `;`
look-around; movement modes; a closed door (open, close, smash, window); a rack with a spear
(wield with `w`); a dummy (melee by walking into it, one sentence on safe mode and `!`); a second
dummy behind a counter (reach attack); spear strap (wear, holster via `a`); gun + magazine + ammo
(`r` reload twice, `f` fire, recoil, `.` to wait); container inventory and a backpack (`i`, `W`);
flashlight + battery (`r`); stairs down (`>`/`<`), darkness, turning on the flashlight (`a`); water
dispenser (`e`, fill bottle); gasoline lantern (remote use); forced hunger -> crafting menu (`&`) ->
peanut butter sandwich -> eat (`E`); construction menu (`*`) painting a wall; forced pain ->
codeine; bubble-wrap trap; counting out baseballs in the pickup menu and throwing (`t`); finale
(action menu `Enter`, `?` keybindings, how to save and quit, "try Friends to the End or Play Now").

What it does not cover (all of which a first real game needs within the first hour):

- The overmap (`m`), what the symbols mean, how to set a destination and auto-travel, what the
  "mission marker" is, what towns look like, that cities are dangerous.
- Time, waiting for a reason (`|`), activities that take game-hours, interruption, the
  distraction manager.
- Sleep (`$`), fatigue/sleepiness, "it's too bright/dangerous to sleep", beds and warmth.
- Reading (`R`) and skills; focus and morale (`@` tabs, `v` morale); what the morale penalty from
  eating nothing but crackers does.
- Thirst from scratch: finding water in a toilet, boiling it, why unpurified water is a problem.
- Listing nearby items and monsters (`V`), the compass widget, and the safe-mode manager (`!`
  held settings in Settings > Safemode); auto-ignore and the "cycle through monsters" flow.
- The advanced inventory (`/`), zones (`Y`) and sort (`O`): the single biggest "how do I move
  stuff" question in every community thread.
- Encumbrance vs. layers vs. warmth on the `@` screen; why you cannot put the jeans in the jeans.
- Butchering: `LESSON_BUTCHER` text exists but there is no corpse in the tutorial (the dummy
  is destroyed, not killed).
- Vehicles: not even "press `^` to control", "e to open a car door", "examine a car tile".
- NPCs and the dialogue screen; the starting NPC in Evacuee.
- Z-levels beyond one staircase; roofs; basements as places to sleep.
- Weather, temperature and wetness, nightfall, light levels outside.
- Health, bleeding, bites and infection, bandages, the first aid flow (`a` on a bandage, pick limb).
- Death: there is no mention of permadeath, memorials, or "Preset Character -> Last Character"
  until the finale paragraph, which is easy to dismiss.

What is broken or stale:

- Dead lessons in the C++ enum (`gamemode_tutorial.h`): `LESSON_WEAPON_INFO`, `LESSON_HIT_MONSTER`
  and the C++ copies of `LESSON_LOOK`, `LESSON_MOVEMENT_MODES`, `LESSON_INTERACT`,
  `LESSON_MONSTER_SIGHTED`, `LESSON_REACH_ATTACK`, `LESSON_HOLSTERS_WEAR`, `LESSON_GUN_LOAD`,
  `LESSON_INVENTORY`, `LESSON_FLASHLIGHT`, `LESSON_REMOTE_USE`, `LESSON_CRAFTING_FOOD`,
  `LESSON_CONSTRUCTION`, `LESSON_THROWING`, `LESSON_FINALE` are never passed to `add_message`
  anywhere in `gamemode_tutorial.cpp`. The data-side EOC versions replaced most of them;
  `LESSON_WEAPON_INFO` and `LESSON_HIT_MONSTER` snippets exist in `snippets.json` and are never shown.
- The `// TODO: clean up old tutorial` in `init()` is still there; the C++/JSON split is a half
  migration, so any change must be made in two places and two formats.
- Popups from the C++ path use `popup()`, which does not go into the message log, so a dismissed
  lesson cannot be re-read (`P`). EOC lessons do log the text, but as a wall in the log.
- `LESSON_GUN_FIRE` fires every turn condition-wise the moment any wielded item has ammo, before
  the gun room if you happen to pick up a loaded item elsewhere.
- Saving in the tutorial warns "Weird things might happen" (`pre_action`); the tutorial is designed
  to be done in one sitting.
- Skills forced to 5 and a forced 20 C mean the first real game feels like a different game.
- No pointer back into the tutorial from a real game, no partial completion, no per-lesson
  re-trigger.
- Historic forum feedback already made the same complaint ("too complex to navigate ... simple
  repeated tasks would fix keybindings in memory better than a dense walkthrough",
  discourse thread 4294, see sources). The 2023-2024 EOC rework fixed content, not shape.

### 1.3 Help screen

`data/core/help.json` is reasonable reference text. Weak spots for beginners:

- The Introduction (4 paragraphs) explains what kind of game it is, not what to do.
- "Survival tips" (11 paragraphs) is the closest thing to a first-hour guide, buried at position 15.
- Nothing describes the sidebar, the `@` screen tabs, the crafting UI, the advanced inventory
  layout, or the aim UI, which are the screens people get lost in.
- Paragraphs reference keys with `<keybind:...>` tags (good, they resolve to the live binding), but
  no paragraph can be opened from the screen it describes.

### 1.4 Verdict

The game has a decent linear tutorial for *verbs* and a decent reference for *rules*, and nothing
that bridges the two into a first real game. The missing layer is exactly what RimWorld's learning
helper and DF Steam's popups provide: context-triggered, persistent, dismissable, re-findable
guidance inside the real game, driven by a checklist of concepts the player has demonstrably done.

---

## 2. What a new player must learn, in the order they need it

Keys are the defaults in `data/raw/keybindings.json`; all text in baby mode must use
`<keybind:...>` tags so remaps are honoured. "Pitfall" is the recurring confusion from the
community threads and from reading the current UI.

### Phase 0: before the first turn (main menu)

| # | Concept | Key / menu | Pitfall |
| --- | --- | --- | --- |
| 0.1 | Which New Game entry to pick | Main menu > New Game > Play Now! (Default Scenario) | Five entries, no explanation. Custom Character's point budget scares people into 20 minutes of menus |
| 0.2 | Permadeath and what "preset > last character" means | New Game > Preset Character | Learned at death, in anger |
| 0.3 | `?` shows the keys for whatever screen you are in | `?` anywhere | Nobody knows; it is the single most important key |

### Phase 1: the first ten minutes (inside the shelter)

| # | Concept | Key / menu | Pitfall |
| --- | --- | --- | --- |
| 1.1 | You are `@`; move with numpad/arrows/vi keys; diagonals matter | movement keys | Arrow keys cannot go diagonal, so arrow players get stuck in doorways and lose turns |
| 1.2 | Every key press is a turn; nothing moves until you do | `.` / `5` to pass a turn | People think the game is frozen |
| 1.3 | Read the sidebar: HP, stamina, hunger/thirst/sleep words, safe mode, weather, time | sidebar | Needs are words ("Peckish") not bars; the sidebar has several layouts (`data/json/ui/sidebar*.json`) |
| 1.4 | Look at things without moving | `;` / `x` look, mouse hover | Everyone tries to walk into things to identify them |
| 1.5 | Examine terrain and furniture | `e` + direction | `e` is "use the world", distinct from `a` "use an item" and `E` "eat"; three different letters |
| 1.6 | Items on the floor, the pickup menu, selecting a count | `g` / `,` pickup; in menu: arrows, number, Enter | Typing numbers vs. `#` mode; not knowing `g` works on adjacent tiles |
| 1.7 | The container inventory: you can only carry what fits in a pocket | `i`, then item letter for details | "Why can't I pick this up" is the #1 shelter question; answer is "wear a backpack / craft a makeshift sling" |
| 1.8 | Wear vs. wield vs. drop vs. take off | `W` wear, `w` wield, `d` drop, `T` take off | `w` vs `W` |
| 1.9 | Open, close, smash | `o`, `c`, `s` + direction; walking into a door opens it | `s` on a window alerts the whole street |
| 1.10 | Light: it is dark in basements and at night; flashlight `a` | `a` apply/activate | People go down the shelter basement unlit |

### Phase 2: the first hour (needs and the world)

| # | Concept | Key / menu | Pitfall |
| --- | --- | --- | --- |
| 2.1 | Thirst and water: toilets and water tanks hold water; fill a container; boil before drinking | `e` on toilet > fill; craft "clean water" with a pot and fire | Drinking unpurified water, or not realising toilets are a water source |
| 2.2 | Hunger: eat; some food is "thinks you could do with" etc.; morale from food | `E` eat | Eating until "Engorged"; vomiting |
| 2.3 | Crafting menu: tabs, the search `/`, "s:" and "q:" filters, red/yellow/green recipes, crafting radius | `&`, `/` in menu | Not knowing search exists; not knowing why a recipe is red (missing tool vs. skill) |
| 2.4 | First crafts: makeshift sling / bag, cudgel or wooden spear, water purification route | `&` | Looting with no storage |
| 2.5 | Time and waiting: hours pass during crafts; interruption; "wait" menu | `|` wait | Starting a 2-hour craft at dusk |
| 2.6 | Sleep: when, where, why "it's too dangerous/bright"; closing curtains | `$` | Trying to sleep at noon in a window |
| 2.7 | Overmap: `m`, symbols, your position, the shelter, the nearest town, notes, set destination / auto-travel | `m`, then `?` in map | Walking off in a random direction; not realising the whole map was revealed around start |
| 2.8 | Reading and skills; books need light and morale; `@` skills tab | `R`, `@` | Not realising books are the main early power |
| 2.9 | The `@` character screen: stats, encumbrance, effects, proficiencies | `@` | Encumbrance numbers unexplained |
| 2.10 | Safe mode: what the message means, `!` to toggle, ignore one monster, the safemode rules screen | `!`, prompt keys | Spamming `!` off and then getting hit while walking |
| 2.11 | Listing what is around you: items / monsters | `V` | Unknown to almost all new players |
| 2.12 | Combat basics: adjacent attack by moving; stamina; retreat through doors; one zombie at a time; never fight in the open at night | movement, `c` close door | Fighting three zombies in a field |
| 2.13 | Pain, HP per limb, bleeding, bites, bandages and disinfectant | `a` on medical item, pick limb; `@` | "Why am I slow" = pain; infected bite ignored = death on day 3 |

### Phase 3: the first few days

| # | Concept | Key / menu | Pitfall |
| --- | --- | --- | --- |
| 3.1 | Advanced inventory: two panes, move stacks, containers, "all around" | `/` | Everyone's #1 "how do I move 40 items" |
| 3.2 | Zones and sorting | `Y`, `O` | Unknown; huge time saver |
| 3.3 | Morale and focus: why learning slows, what cheers you up | `v` morale, focus widget | Focus at 20 after days of crackers |
| 3.4 | Aiming and firing: aim levels, `f` then `.`/`a`/`c`/`p`, noise | `f` | Firing from the hip at range; waking the town |
| 3.5 | Reloading, magazines, unloading | `r`, `U` | Magazine vs. gun confusion (already in tutorial) |
| 3.6 | Butchering, field dressing, tainted meat | `B` | Eating zombie meat |
| 3.7 | Fire: lighting, fuel, cooking, indoor fire danger | `a` lighter / `e` on brazier | Burning the shelter down |
| 3.8 | Weather and temperature, wetness, towels, layers | sidebar, `@` | Hypothermia in spring rain |
| 3.9 | Vehicles: examining, opening doors, driving (`^`), the vehicle interaction menu | `e` on vehicle, `^` | Driving into a wall at 60 mph |
| 3.10 | Z-levels, stairs, roofs, basements, `<`/`>` | `<` `>` | Not knowing houses have basements |
| 3.11 | NPCs: talk, trade, follow, "don't shoot them" | `C` chat; `e` on NPC | Attacking the starting NPC with safe mode off |
| 3.12 | Missions and notes | `M`, `N` on overmap | Ignoring the starting mission |
| 3.13 | Death, memorials, starting again with the same character | main menu | Quitting the game forever |

---

## 3. Design: baby mode

### 3.1 Shape, in one paragraph

Baby mode is not a new tutorial map. It is an **overlay on a normal game**: a persistent checklist
of concepts ("lessons"), each with a trigger (a game event or state), a one-paragraph explanation
with live key names, a "done when" condition, and a one-line pointer to the help paragraph. The
overlay shows a small **Next step** panel in the sidebar, raises a **hint** (non-modal by default,
popup only for the first few) when a lesson's trigger fires, marks lessons done automatically when
the player performs the action, and offers an **"Explain this screen"** key. It is a world/character
option, defaults on for Play Now, can be toggled off at any time, and every hint can be dismissed
or snoozed. Game rules do not change. A **soft start** (a curated scenario/profession preset and a
few safe-start options) is layered on top but is optional and is just data.

This is the RimWorld learning-helper shape (lessons activate "when something happens relating to a
concept the player hasn't learned", are "marked learned when the player does the necessary
interaction", and can be browsed and searched), with Dwarf Fortress Steam's "don't show again" and
help-screen index, and with Caves of Qud's choice to keep a scripted tutorial tiny (move, fight,
drink, eat) and hand off to the real game quickly. The existing Tutorial Game stays as the optional
"practice room" and is linked from the checklist.

### 3.2 Components

1. **Lesson data** (`data/json/onboarding/lessons.json`, new JSON type `onboarding_lesson`):

   ```jsonc
   {
     "type": "onboarding_lesson",
     "id": "ob_pickup",
     "phase": 1,                       // ordering bucket, see section 2
     "order": 6,
     "title": "Pick things up",
     "short": "Stand on items and press <keybind:pickup>.",
     "text": "You are standing on something.  Press <keybind:pickup> ... (3-5 sentences)",
     "help_category": "Items overview", // opens data/core/help.json category
     "trigger": { "event": "avatar_moves", "condition": { "u_at_items": true } },   // OR an EOC id
     "done_when": { "event": "character_picks_up_item" },                          // OR a condition
     "presentation": "hint",           // "hint" (sidebar + log), "popup" (modal, first 6 only), "silent" (checklist only)
     "prerequisites": [ "ob_move" ],
     "snooze_turns": 600
   }
   ```

   Triggers and done-conditions reuse the dialogue condition language already parsed by
   `read_condition()` (`src/condition.cpp`), so authors can write `{ "math": [ "u_val('thirst') > 40" ] }`
   or `{ "u_has_effect": "hunger_hungry" }` without new C++ for each lesson. Event triggers reuse
   `event_type` names (`src/event.h`) exactly like `"required_event"` on an `EVENT` EOC. The
   `<keybind:...>` and `<color_...>` tags are already expanded by `parse_tags()` as in
   `tutorial_game::add_message`.

2. **Lesson manager** (`src/onboarding.cpp/.h`, new): a singleton `event_subscriber` registered on
   `get_event_bus()` (`src/event_bus.h:22`), plus a cheap per-turn poll for state-only triggers
   (hooked next to `gamemode->per_turn()` in `src/do_turn.cpp:548`, guarded by the option). Holds
   per-lesson state `{ unseen, active, snoozed_until, done, dismissed }`, serialised with the
   avatar (`avatar::store/load` in `src/savegame_json.cpp`) so it survives save/load and is per
   character. It decides which single lesson is "current" (lowest phase/order that is active and
   not done) and keeps at most three active at once, so the player is never flooded.

3. **Presentation**
   - *Hint*: `add_msg( m_info, ... )` with a distinct prefix and colour into the normal message log,
     plus the sidebar panel update. Re-readable with `P`.
   - *Popup*: `query_popup` (`src/popup.h:38`) with options "Got it", "Show me in Help",
     "Later", "Stop hints" (the last toggles the option). Used only for `presentation: "popup"`
     lessons: welcome, move, look, sidebar, safe mode, first monster, first night.
   - *Checklist panel*: a new `widget` var `onboarding_text` in `src/widget.h` (`widget_var`) /
     `src/widget.cpp`, rendered through the existing sidebar system (`src/panels.cpp:373-450`
     builds panels from `"style": "sidebar"` widgets). Add `data/json/ui/onboarding.json` with
     a 3-5 line text widget ("Next: Pick things up  [g]  (3/14 done)  ? to explain") and add it
     to the default layouts in `data/json/ui/sidebar.json` behind a clause so it collapses to
     nothing when baby mode is off. Clauses already support dialogue conditions
     (`widget_clause::meets_condition`, `src/widget.cpp:344`), so the widget can hide itself with
     `{ "math": [ "u_onboarding_enabled == 1" ] }` even before the C++ var exists.
   - *Checklist screen*: a `cataimgui::window` (`src/cata_imgui.h:207`) or a `uilist`
     (`src/uilist.h`) listing all lessons grouped by phase with done/active/locked marks; select
     one to read its text, "mark done", "re-trigger", or jump to help. Bound to a new action
     `onboarding_list` (suggest `F1`, unbound today) and reachable from the action menu and the
     main menu Help entry.
   - *Explain this screen*: a new global action `EXPLAIN_SCREEN` (suggest `F2`). In the default
     mode it opens the checklist screen on the current lesson. Inside other screens it is handled
     by `input_context`: `input_context` already knows its category string (`"INVENTORY"`,
     `"CRAFTING"`, `"ADVANCED_INVENTORY"`, `"TARGET"`, `"OVERMAP"`, ...), so a map
     `category -> help paragraphs / lesson id` in `data/json/onboarding/screens.json` lets one
     generic handler show the relevant paragraph. Minimal first version: when `EXPLAIN_SCREEN`
     is pressed in any context, show a `popup` with that context's `screens.json` text and the
     existing keybinding list underneath.

4. **Toggle and scope**
   - World/game option `ONBOARDING_HINTS` (`src/options.cpp`, Interface page) with values
     `off / checklist_only / hints / hints_and_popups`. Default `hints_and_popups` for a character
     created with Play Now, `checklist_only` otherwise (set in `main_menu.cpp:1042-1054` where
     `play_type` is chosen). A one-line prompt on the first turn ("New here? Keep hints on?
     y/n") covers the rest.
   - Per-lesson "don't show again" and global "Stop hints" everywhere a hint is shown, as in DF.
   - Nothing in the lesson manager mutates game state: it reads events and conditions and writes
     only its own state and messages.

5. **Soft start** (data only, optional)
   - A scenario `Evacuee (guided)` copied from `evacuee` with `"start_name": "Evac shelter (guided)"`
     and a profession `Survivor (guided)` that adds a backpack, a flashlight with battery, a
     bottle of water and a pocket knife, so lessons 1.7 and 1.10 are demonstrable without luck.
     Points are irrelevant because Play Now ignores them. No rule change: the scenario is a
     normal scenario anyone could pick.
   - Baby-mode Play Now preselects this scenario (one branch in `newcharacter.cpp` where
     `character_type::NOW` picks the default scenario).
   - Optional world options the preset can suggest but not force: spawn rate 0.5, "surrounded
     start" off, static NPCs on (so the starting NPC exists to teach dialogue).

6. **The existing Tutorial Game** becomes lesson `ob_practice_room` ("Want to practise in a safe
   room first? Main menu > Tutorial Game"), and `gamemode_tutorial.cpp` is left alone in
   milestone 1. In milestone 4 the dead enum entries are removed and the remaining C++ lessons
   are moved to EOCs so there is one format.

### 3.3 The lesson script (first version, 30 lessons)

Phase 0 and 1 popups are limited to six. Everything else is a hint plus checklist entry.

| Id | Trigger | Done when | Presentation |
| --- | --- | --- | --- |
| ob_welcome | `game_start` with baby mode on | dismissed | popup: what the screen is, `?` for keys, `F1` for the checklist, `F2` to explain any screen, you can turn this off |
| ob_move | `game_start` | 5 `avatar_moves` | popup (the current LESSON_MOVE text, trimmed) |
| ob_pass_turn | after ob_move | one `ACTION_PAUSE` | hint |
| ob_sidebar | after ob_move | dismissed or `F2` used | popup: what each sidebar line means, that needs are words |
| ob_look | after ob_move | `ACTION_LOOK` or mouse view opened | hint |
| ob_examine | adjacent to furniture with an examine action (poll) | `ACTION_EXAMINE` | hint |
| ob_pickup | standing on items (poll, same as `per_turn` today) | `ACTION_PICKUP` | hint |
| ob_inventory | first pickup | `ACTION_INVENTORY` | hint: container inventory, "your pockets are full" meaning |
| ob_storage | pickup refused for volume, or 10 turns after ob_inventory | `character_wears_item` with a container | hint: wear a backpack or craft a makeshift sling (`&`, search "sling") |
| ob_wear_wield | first armour or weapon picked up (`post_action` logic from the tutorial) | `character_wears_item` / `character_wields_item` | hint |
| ob_doors | adjacent to door (poll) | `ACTION_OPEN` + `ACTION_CLOSE` | hint |
| ob_safe_mode | first safe-mode "Spotted" message (hook in `src/game.cpp:7412`) | `ACTION_TOGGLE_SAFEMODE` or ignore used | popup: what it means, three choices, do not just turn it off |
| ob_first_monster | first hostile in view (same hook) | monster dead or out of view | popup: retreat indoors, close the door, fight one at a time by walking into it |
| ob_pain_hp | `character_takes_damage` | `ACTION_PL_INFO` | hint: `@`, limb HP, pain slows you |
| ob_bleeding_bite | `character_gains_effect` bleed / bite | effect gone | hint: bandage with `a`, disinfect bites, infection kills |
| ob_thirst | `u_has_effect` thirst_thirsty | `character_consumes_item` drink | hint: toilets and tanks, fill with `e`, boil first |
| ob_hunger | hunger_hungry | `character_eats_item` | hint |
| ob_crafting | ob_storage or ob_thirst active for 50 turns | `ACTION_CRAFT` | hint: `&`, tabs, `/` search, colours |
| ob_time_wait | first craft longer than 10 minutes started (`character_starts_activity`) | activity finished | hint: time passes, `|` waits, interruptions |
| ob_light_night | light level low at player (`g->light_level`) | flashlight on or sleeping | hint: night is deadly outside, flashlight `a`, close curtains |
| ob_sleep | sleepiness Tired | `character_falls_asleep` | hint: `$`, needs dark and safe, bed |
| ob_overmap | 30 minutes played or player leaves the start OMT (`avatar_enters_omt`) | `ACTION_MAP` | hint: `m`, symbols, your shelter, notes, destination |
| ob_list_around | second hostile sighting | `ACTION_LIST_ITEMS` | hint: `V` |
| ob_read_skills | first book picked up | `reads_book` | hint |
| ob_morale_focus | focus < 50 or morale < -20 | `ACTION_MORALE` | hint |
| ob_adv_inventory | 30 items picked up, or a drop of >5 items | `ACTION_ADVANCEDINV` | hint: `/` |
| ob_aim_fire | gun wielded with ammo | `character_ranged_attacks_monster` | hint: `f`, aim levels, noise |
| ob_butcher | standing on a non-zombie corpse (poll) | `character_butchered_corpse` | hint |
| ob_vehicle | adjacent to a vehicle (poll) | vehicle interaction opened | hint: `e` to open doors, `^` to drive, do not drive yet |
| ob_npc | NPC in view | dialogue opened | hint: `C`, do not attack, safe mode protects you |
| ob_death | `avatar_dies` / `game_avatar_death` | n/a | popup on the death screen: permadeath, memorial, Preset > Last Character, what killed you and the lesson for it |

### 3.4 Hooks, file by file

| Need | Existing code to hook | Notes |
| --- | --- | --- |
| Receive events | `src/event_bus.h` `event_bus::subscribe`, `event_subscriber::notify`; `src/event.h` `event_type` | Same mechanism `eoc_events` (`src/effect_on_condition.h:37`) and `stats_tracker` use |
| Per-turn state polls | `src/do_turn.cpp:548` next to `gamemode->per_turn()` | Keep to adjacency scans and a few `u_val`s; cost is negligible |
| Action-keyed triggers | `src/handle_action.cpp:3403/3425` (`gamemode->pre_action/post_action`) | Add `onboarding::on_action(act)` beside them, or route the same `action_id` through the manager; this is how "done when ACTION_X" is detected without new events |
| Safe-mode and first monster | `src/game.cpp:7412` (spotted message), `src/avatar_action.cpp:406` (not attacking) | Call `onboarding::trigger("ob_safe_mode")` there |
| Conditions in JSON | `src/condition.cpp` `read_condition`, `dialogue` with avatar talker | Same parser widgets and EOCs use |
| Text with live key names | `parse_tags()` in `src/text_snippets.cpp` / `src/output.h`; `<keybind:...>` tags | Already used by the tutorial |
| Modal popups with options | `src/popup.h` `query_popup::context().option()` | For "Got it / Help / Later / Stop" |
| Log hints | `src/messages.h` `add_msg( game_message_params, ... )` | Use `m_info`; consider a new `m_hint` type for colour |
| Sidebar panel | `src/widget.h` `widget_var` + `src/widget.cpp` value/text functions; `src/panels.cpp` sidebar layouts; `data/json/ui/*.json` | Add `onboarding_text` var and a widget; hide via clause when disabled |
| Checklist screen | `src/cata_imgui.h` `cataimgui::window` (same base as `help_window` in `src/help.cpp`) or `src/uilist.h` | Reuse `help_window::draw_category` rendering for lesson text |
| Explain this screen | `src/input_context` category name; new action id in `data/raw/keybindings.json` (global category); `help_window` for the deep link | A `help_window::show_category(name)` entry point is a small addition to `src/help.cpp` |
| Option | `src/options.cpp` Interface page `add(...)` | `ONBOARDING_HINTS` enum option |
| Default on for Play Now | `src/main_menu.cpp:1024-1054` | Set the option/flag when `play_type == character_type::NOW` |
| Persistence | `src/savegame_json.cpp` (avatar serialisation) | Map lesson id -> state; unknown ids ignored on load |
| Death screen | `game::death_screen` / memorial flow in `src/game.cpp` | Show `ob_death` before the memorial |
| Soft-start data | `data/json/scenarios.json`, `data/json/professions.json`, `data/json/start_locations.json` | Pure JSON |
| Practice room | `src/gamemode_tutorial.cpp`, `data/mods/dda_tutorial` | Unchanged in M1; cleanup in M4 |

Everything above is additive; nothing changes a rule, a spawn, a recipe or a stat.

### 3.5 Why not just extend the Tutorial Game

The corridor tutorial already teaches verbs well, and the next ten verbs people need (overmap,
sleep, water, crafting search, advanced inventory, safe mode) only make sense in the real world
with real pressure. A longer corridor does not transfer: the forum complaint from 2014 and the
Qud team's decision to keep their tutorial to "move, fight, drink, eat" point the same way.
Keep the corridor, make it optional, and put the teaching where the player actually is.

### 3.6 Alternatives considered

- **Pure EOC/JSON implementation with no C++.** Possible for triggers (`EVENT` EOCs with
  `required_event`, `RECURRING` EOCs for polls, `u_message` with `popup`, `u_*` math vars for state)
  and even for the sidebar (a `text` widget with clauses on `u_ob_*` vars). It cannot add a
  checklist screen, an "explain this screen" key, options, or per-action detection, and the
  message log would be the only history. It is the right way to **prototype the lesson texts and
  triggers** (milestone 1 is written so it can be done that way), and the C++ manager should read
  the same JSON so the prototype content is not thrown away.
- **Achievements as the checklist.** `event_statistics` + `achievement` give persistent,
  event-driven completion with a UI already in the game. They cannot express "adjacent to a door"
  or "pressed `V`" without new events, and their UI is a trophy list, not a next-step panel. Reuse
  the event plumbing, not the UI.
- **A mod (`dda_babymode`) instead of core.** Data parts can be a mod; the C++ parts cannot. Since
  this repo is a fork, core is fine, and keeping the lesson data under `data/json/onboarding/`
  lets the mods override or extend it later with the normal `copy-from`.

### 3.7 Interaction with the 3D renderer and first-person mode

Hints are text; they work in any view. Two things to keep in mind: the sidebar panel must survive
whatever layout the 3D view uses (if the sidebar is hidden in 3D, show the "Next step" line as an
ImGui overlay instead), and "explain this screen" should recognise the 3D view's own input context
if it introduces one, so the camera keys get explained too.

---

## 4. Milestones

Estimates assume one developer familiar with the codebase; double them for a first contribution.
Each milestone is independently shippable and toggleable.

### M1: Checklist of the first ten actions with contextual hints (2-4 days)

Delivers: an option `ONBOARDING_HINTS`; `onboarding_lesson` JSON type with 10-12 Phase 0/1 lessons
(welcome, move, pass turn, sidebar, look, examine, pickup, inventory/storage, wear/wield, doors,
safe mode, first monster); an `event_subscriber` + per-turn poll + `on_action` hook; hints in the
message log; a one-line sidebar widget "Next: ... (n/12)" in the default layouts; auto-completion
when the action is performed; welcome and safe-mode popups via `query_popup` with Got it / Later /
Stop. State saved with the avatar. Default on for Play Now.

Smallest useful thing: a player who presses Play Now sees what to press next for the first ten
minutes and is told what the safe-mode message means. If time is short, the sidebar widget can be
an EOC-driven text widget and the lessons `EVENT`/`RECURRING` EOCs in a data file, with no C++
beyond the option; that is 1-2 days and throwaway only in the parts that move to C++ later.

### M2: Phase 2 lessons, checklist screen, explain-this-screen (3-5 days)

Delivers: the remaining Phase 2 lessons (thirst, hunger, crafting and search, time/wait,
night/light, sleep, overmap, `V`, reading, morale/focus, pain/HP/bleeding); the checklist ImGui
window on `F1` with phase grouping, mark done / re-show / open help; `EXPLAIN_SCREEN` on `F2` with
`screens.json` for the default mode, inventory, crafting, advanced inventory, aim, overmap, `@`
and the pickup menu; `help_window::show_category()` deep link; death-screen lesson.

### M3: Soft start and Phase 3 (2-3 days)

Delivers: `Evacuee (guided)` scenario + `Survivor (guided)` profession; baby-mode Play Now
preselects it; suggested world options shown once; Phase 3 lessons (advanced inventory, zones,
aim/fire, reload, butcher, fire, weather, vehicle, NPC, missions); the Tutorial Game linked as the
practice-room lesson.

### M4: Consolidation (2-3 days)

Delivers: dead `tut_lesson` entries removed and remaining C++ tutorial lessons moved to EOCs so the
practice room and baby mode share one format; help.json gains short "what this screen shows"
paragraphs per screen and a "Your first hour" category placed first; a `tests/onboarding_test.cpp`
that loads the lesson JSON, checks every `<keybind:>` tag resolves and every `help_category`
exists, and runs the trigger/done conditions on a fixture avatar.

### M5 (optional): Telemetry-free tuning (1-2 days)

Delivers: a debug command that dumps lesson timings (turn first shown, turn done, dismissed) to the
memorial folder, so you and your friend can play two first games and see where hints came too
early, too late, or were ignored. No network.

---

## 5. Open questions for the user

1. **Who is the target: you two now, or new players in general?** If it is you two, M1 plus
   the soft start is enough and the checklist screen can wait. If it is for a wider audience,
   M2's explain-this-screen is the part that pays off most.
2. **Popups vs. hints.** The design uses six modal popups total. Do you want zero (log + sidebar
   only, RimWorld style) or more (DF style, one per new screen)?
3. **Keys.** `F1` and `F2` are suggested for checklist and explain-screen because they are unbound.
   Any objection, and should `?` in the default mode open the checklist first with the keybinding
   list one tab over?
4. **Soft start contents.** Is a guaranteed backpack, flashlight, water and knife acceptable, or do
   you want the real Evacuee start (empty shelter) with the lessons pointing at the makeshift
   sling recipe instead? The second is more honest about the real game; the first removes the
   most common day-one frustration.
5. **Where does the sidebar line go in the 3D view?** Is the sidebar visible in your renderer
   layout, or should the "Next step" line be an ImGui overlay on the terrain window?
6. **JSON-only prototype first?** It can be done in a day with EOCs and a text widget, which lets
   you tune texts and triggers by editing data, before committing to the C++ manager. Worth it if
   you expect to iterate on wording a lot.
7. **Scope of "done when".** Should lessons auto-complete only on the exact action (strict, DF
   style) or also when the player clearly already knows it (e.g. mark "doors" done if they open
   five doors before the hint fires)? The second needs a "quietly done" state so the checklist
   does not nag experienced players who left the option on.
8. **Should baby mode ever touch rules?** This document says no. If you later want "safe mode
   cannot be fully disabled in baby mode" or "no night-one hordes", that is a different feature
   and should be a separate, clearly labelled world option.

---

## 6. Sources

In-repo (paths relative to `/home/user/Cataclysm-DDA-3D`):

- `src/gamemode_tutorial.cpp`, `src/gamemode_tutorial.h`, `src/gamemode.cpp`
- `data/mods/dda_tutorial/{modinfo,snippets,traps,mapgen,monsters,overmap_terrain,specials}.json`
- `src/main_menu.cpp` (menu items 462-482, tutorial launch 842-877, new-game flow 1024-1064)
- `src/newcharacter.cpp` (`Character::randomize` 428ff, `avatar::create` 773ff)
- `src/help.cpp`, `data/core/help.json`
- `data/motd/en.motd`
- `src/game.cpp` (`start_game` 790-1075, safe-mode message 7412), `src/avatar_action.cpp:406`
- `src/do_turn.cpp:548`, `src/handle_action.cpp:3403,3425`
- `src/event.h`, `src/event_bus.h`, `src/effect_on_condition.h`, `doc/JSON/EFFECT_ON_CONDITION.md`
  (`u_message` popup fields, event table)
- `src/widget.h`, `src/widget.cpp`, `src/panels.cpp`, `data/json/ui/*.json`
- `src/popup.h`, `src/uilist.h`, `src/cata_imgui.h`, `src/messages.h`, `src/options.cpp`
- `data/raw/keybindings.json` (default keys quoted in section 2)
- `data/json/scenarios.json`, `data/json/achievements.json`, `src/achievement.cpp`,
  `src/event_statistics.h`

Community and other games (reached via web search summaries; direct page fetches were blocked
from this environment, so quotes are as reported by the search results):

- CDDA forum, "The tutorial is not at all user-friendly for newbies":
  https://discourse.cataclysmdda.org/t/the-tutorial-is-not-at-all-user-friendly-for-newbies/4294
- CDDA forum, "Words of advice to new players?":
  https://discourse.cataclysmdda.org/t/words-of-advice-to-new-players/16129
- CDDA forum, "Good place to start" (wiki guides index, "crafting isn't up to date"):
  https://discourse.cataclysmdda.org/t/good-place-to-start/8350
- CDDA forum, "How hard was the learning curve for you guys?":
  https://discourse.cataclysmdda.org/t/how-hard-was-the-learning-curve-for-you-guys/15698
- CDDA forum, "Getting used to crafting": https://discourse.cataclysmdda.org/t/getting-used-to-crafting/11644
- CDDA forum, "Tips, tricks and newb questions" megathread:
  https://discourse.cataclysmdda.org/t/tips-tricks-and-newb-questions/42
- CDDA forum, "Cataclysm DDA 0.D Beginner Tutorial (+Tips&Tricks)":
  https://discourse.cataclysmdda.org/t/cataclysm-dda-0-d-beginner-tutorial-tips-tricks/20412
- Steam community, CDDA veteran tips thread (zones `Y`/`O`, bag whitelists, favourites, `?` in
  every menu, unbound QoL actions, wooden spear):
  https://steamcommunity.com/app/2330750/discussions/0/6197556602759206593 and
  https://steamcommunity.com/app/2330750/discussions/0/4034727291144392999
- CorrosionHour, "Cataclysm: Dark Days Ahead - 10 Tips for Getting Started" (makeshift sling,
  curtains, crouching):
  https://www.corrosionhour.com/cataclysm-dark-days-ahead-10-tips-for-getting-started/
- SteamAH beginner guide (do not spend time on a custom character first):
  https://steamah.com/cataclysm-dark-days-ahead-beginners-guide-to-start/
- Independent Software, "Surviving the early game" (evac shelter, do not enter the basement
  unarmed): https://www.independent-software.com/cataclysm-dark-days-ahead-surviving-early-game.html
- Hitchhiker's Guide to the Cataclysm (community item/recipe data browser, not a tutorial; from
  prior knowledge, the search engine did not index it): https://cdda-guide.nornagon.net
- RimWorld wiki, "Learning helper": https://rimworldwiki.com/wiki/Learning_helper
- Ludeon blog, "Features preview of Alpha 15" (learning helper design statement) and Alpha 15
  release notes: https://ludeon.com/blog/?p=975 , https://ludeon.com/blog/?p=996
- Dwarf Fortress Steam discussions, "How to turn off tutorial pop ups?" (per-popup "don't show
  again", help screen lists unseen tutorials):
  https://steamcommunity.com/app/975370/discussions/0/3727324132823018161/
- GamingOnLinux, DF Steam release review (short guided tutorial as a welcome addition):
  https://gamingonlinux.com/2022/12/dwarf-fortress-arrives-on-steam-works-on-linux-desktop-and-steam-deck
- Freehold Games devlog, "Tutorial beta out now!" (Caves of Qud):
  https://freeholdgames.itch.io/cavesofqud/devlog/809524/tutorial-beta-out-now
- FullCleared, "Caves of Qud tutorial beta now available":
  https://fullcleared.com/news/caves-of-qud-tutorial-beta-now-available/
- Stop Caring, "Your thirst is mine, my water is yours" (Qud tutorial covers move, fight, eat,
  drink and then stops): https://www.stopcar.ing/your-thirst-is-mine-my-water-is-yours/
- Brogue overview (auto-explore, examine from a distance, remappable keys, simplicity):
  https://www.linuxlinks.com/Brogue/ and https://en.wikipedia.org/wiki/Brogue_(video_game)
