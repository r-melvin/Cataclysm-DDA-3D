# Renderer comparison sessions

Build the tiles executable and compose `Ultica_iso` and `UltimateCataclysm` with
`tools/r3d_spike/compose_tilesets.sh Ultica_iso UltimateCataclysm`, then run:

```sh
python3 tools/r3d_spike/compare_session.py 'My world' --output /tmp/my-renderer-report
# For saves outside this checkout, add --userdir /path/to/user-directory.
# Omit the world to choose/create a new world in the private session.
```

Play normally and quit normally. The launcher does not synthesize keys, control
windows, or change desktop focus. It clones the chosen world and a small list of
configuration files into `/tmp/my-renderer-report/user`; your original saves are
untouched. **All progress during this session remains in that private user
directory.** Keep it if you want to continue that character. Launch the same
executable with `--userdir /tmp/my-renderer-report/user/` to resume it.

The default is five checkpoints at least 30 seconds apart (`--count`, `--interval`).
A checkpoint happens when gameplay next asks for a normal player action. Menus,
activities and auto-movement do not trigger capture. Idle waiting does not cause
new checkpoints; take a normal action after the interval. Each batch briefly
pauses play while it saves, reloads tilesets, and draws screenshots. No game turn
or input is processed inside that batch; queued keys remain for gameplay. The
previous view, tileset, zoom, camera, pan and gameplay RNG are restored afterwards.

Each completed checkpoint includes a **fresh save of the captured state**, a map
dump, build/OS/graphics/language/mod diagnostics and the options active at capture time, one fixed isometric 2D reference,
and eight orthographic 3D poses at 45-degree yaw intervals and 30-degree pitch.
Isometric 2D has a fixed orientation; its reference is shared by all poses rather
than pretending that 2D can rotate. `view_before.png` and `view_resumed.png` record the player view before and after
the batch for restoration checks.
No complete marker is written if saving or either renderer fails. A genuine disk-write
failure may show the normal game save error dialog; dismiss it normally. The
checkpoint is aborted and its older disk save is never packaged as current state.
An unsupported (save-dirty) game or unavailable checkpoint destination fails
without opening a save dialog. Capture saves
omit the gameplay save event, so recording does not inflate save-event statistics.

The save reproduces the scene on the recorded build with the same mods and assets.
Custom mod/tileset assets are not bundled. `rng.txt` records the pre-capture random
engine for investigation; normal game loading does not restore that engine, so
future action sequences are not promised to replay deterministically. Animated
sprite phases can differ. Screenshots include the whole game window, including UI.

After quitting, numbered `comparison-001.zip`, etc. are created in the session
directory. Each is a normal ZIP below GitHub's 25 MB attachment limit. Upload all
of them to the issue. To reproduce, extract all ZIPs into one empty directory and
run `python3 restore.py` (reassembles any oversized files), then follow README.txt
to load a checkpoint in a new user directory (copy both its `save/` and `config/`). No saves are silently trimmed.
If the game crashes, keep the private directory and package completed checkpoints:

```sh
python3 tools/r3d_spike/compare_session.py --package /tmp/my-renderer-report
```

Packaging refuses to overwrite earlier ZIPs; move those aside before repackaging.
Checkpoints with failures are retained privately for diagnosis and excluded from
shareable archives. Inspect reports before sharing: the selected world's saves
include characters, notes, and explored map data, and logs may include local paths.
The package includes only completed checkpoint directories, session logs,
`options.json`, and reports; it does not sweep the checkout or other worlds.

The upstream bug template requests a save, reproduction steps, expected behavior,
screenshots, versions/configuration and crash/debug logs. The fork's simple master
template allows plain descriptions and optional attachments. The generated report
provides the technical attachments; add your description, reproduction steps and
expected behavior. [GitHub's official attachment documentation](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/attaching-files)
(checked 2026-10-18) accepts `.zip`, `.gz`, `.tgz`, limits non-image files to 25 MB,
and separately limits images to 10 MB.

The older `compare_iso.sh` remains a one-shot capture-and-exit helper. For normal
play use `compare_session.py`. Direct `CATA_R3D_COMPARE_SESSION` environment use
writes saves to the active user directory; the launcher is the supported way to
keep originals isolated.

The launcher checks the executable's capture protocol before opening the game;
**updating this script also requires rebuilding the executable**. Use `--binary`
if your fresh build is outside the checkout. On macOS the build must include Metal
shader artifacts, normally generated by `make TILES=1 BUILD_SHADER_FORMATS=msl`
(with the other options from your usual build). Install `glslangValidator` and
`shadercross` as described in the SDL3 compiling guide if shader generation fails.
The launcher reports missing artifacts before play. An explicit `SDL_GPU_DRIVER`
selects that backend's artifacts instead.

For the private game process, the launcher selects SDL's `gpu` renderer and enables
map tiles in the copied options; those are needed for comparison captures. Source
options are untouched. Other rendering options, including the player's initial
2D/3D view, retain their values. The terminal prints capture progress and reports
failed stages with private log paths. If no checkpoints complete, no ZIP is made.

Omitting the world starts with **no existing worlds** in the private directory.
Create a world and character and enter gameplay before quitting. To capture an
existing character, name its world on the command line; add `--userdir` pointing
to the directory containing its `save/` folder when it is outside this checkout.
Exiting the title screen or character creation never produces a checkpoint.
