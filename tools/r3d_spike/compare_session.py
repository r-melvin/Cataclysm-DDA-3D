#!/usr/bin/env python3
"""Play in an isolated user directory, collect paired renderer checkpoints, and share ZIPs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CONFIG_FILES = ("options.json", "keybindings.json", "auto_pickup.json", "safemode.json",
                "auto_note.json", "colors.json", "base_colors.json", "custom_colors.json")
MAX_PAYLOAD = 24_000_000  # GitHub says 25 MB; reserve space for ZIP headers.
CHUNK = 20_000_000
RESTORE = '''#!/usr/bin/env python3
"""Run after extracting ALL numbered ZIPs into the same empty directory."""
import hashlib, json
from pathlib import Path
root = Path(__file__).resolve().parent
for entry in json.loads((root / 'large-files.json').read_text()):
    target = root / entry['path']
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('wb') as out:
        for part in entry['parts']:
            out.write((root / part).read_bytes())
    assert hashlib.sha256(target.read_bytes()).hexdigest() == entry['sha256'], target
print('Save files restored. See README.txt for loading instructions.')
'''


def copy_tree(source, target):
    """Copy regular files only; never traverse links out of the selected world."""
    if source.is_symlink():
        raise ValueError(f"Refusing symlink: {source}")
    for directory, dirs, files in os.walk(source, followlinks=False):
        relative = Path(directory).relative_to(source)
        (target / relative).mkdir(parents=True, exist_ok=True)
        for name in dirs + files:
            if (Path(directory) / name).is_symlink():
                raise ValueError(f"Refusing symlink: {Path(directory) / name}")
        for name in files:
            src = Path(directory) / name
            if src.is_file():
                shutil.copy2(src, target / relative / name)


def package(session):
    """Only publish completed checkpoints and explicitly selected diagnostics."""
    captures = session / "captures"
    completed = [p for p in sorted(captures.glob("capture-*"))
                 if (p / "COMPLETE.txt").is_file() and not (p / "FAILED.txt").exists()]
    if not completed:
        raise ValueError("No complete checkpoints; inspect the private session logs. No archive made.")
    with tempfile.TemporaryDirectory(prefix="r3d-package-") as temporary:
        staging = Path(temporary)
        for capture in completed:
            copy_tree(capture, staging / "captures" / capture.name)
        diagnostics = staging / "diagnostics"
        diagnostics.mkdir()
        for name in ("debug.log", "debug.log.prev", "crash.log"):
            src = session / "user" / "config" / name
            if src.is_file() and not src.is_symlink():
                shutil.copy2(src, diagnostics / name)
        for name in ("session.json", "stdout.log"):
            src = session / name
            if src.is_file() and not src.is_symlink():
                shutil.copy2(src, diagnostics / name)
        # Configuration is allowlisted; never include keybindings, other worlds, caches,
        # credentials, arbitrary files in the checkout, or unrelated personal configuration.
        src = session / "user" / "config" / "options.json"
        if src.is_file() and not src.is_symlink():
            shutil.copy2(src, diagnostics / "options.json")
        (staging / "README.txt").write_text(
            "Renderer comparison report\n\n"
            "Extract ALL numbered ZIPs into the same empty directory, then run python3 restore.py.\n"
            "Each captures/capture-NNNN is an independent same-state checkpoint. Copy its save/\n"
            "directory into a NEW user directory, then launch the recorded build with --userdir\n"
            "that directory and --world WORLD. Do not overwrite your normal saves. Custom mods\n"
            "and tileset assets are not included; use the versions listed in game-report.txt.\n"
            "rng.txt records the gameplay RNG for investigation; normal loading initializes RNG\n"
            "normally. Saves reproduce the scene, not a deterministic future action sequence.\n\n"
            "view_2d.png is the fixed Ultica_iso reference for every 3D pose in this checkpoint.\n"
            "view_3d_yaw_N.png uses yaw -45 + 45*N degrees, pitch 30, orthographic projection.\n"
            "view_resumed.png shows the restored player view after the batch.\n"
            "No turn or input is processed between screenshots. Capture briefly pauses play.\n\n"
            "For a useful issue, describe the bug, reproduction steps and expected behavior.\n"
            "Attach these ZIPs; game-report.txt records OS/build/graphics/language/mods.\n"
            "Inspect before sharing: saves/logs can contain character names, notes and paths.\n"
            "This bundle intentionally includes only completed snapshots of the selected world,\n"
            "the game report, options, and session debug/crash/stdout logs.\n\n"
            "GitHub supports ZIP attachments up to 25 MB (images separately: 10 MB), checked\n"
            "2026-10-18: https://docs.github.com/en/get-started/writing-on-github/"
            "working-with-advanced-formatting/attaching-files\n"
        )
        large = []
        for file in sorted(staging.rglob("*")):
            if not file.is_file() or file.stat().st_size <= CHUNK:
                continue
            relative = file.relative_to(staging).as_posix()
            parts = []
            digest = hashlib.sha256()
            with file.open("rb") as source:
                while data := source.read(CHUNK):
                    digest.update(data)
                    name = f"chunks/{len(large):04d}-{len(parts):04d}.bin"
                    target = staging / name
                    target.parent.mkdir(exist_ok=True)
                    target.write_bytes(data)
                    parts.append(name)
            large.append(dict(path=relative, parts=parts, sha256=digest.hexdigest()))
            file.unlink()
        (staging / "large-files.json").write_text(json.dumps(large, indent=2))
        (staging / "restore.py").write_text(RESTORE)
        archives = []
        archive = None
        payload = 0
        try:
            for file in sorted(p for p in staging.rglob("*") if p.is_file()):
                size = file.stat().st_size + 2 * len(str(file.relative_to(staging)).encode()) + 512
                if archive is None or payload + size > MAX_PAYLOAD:
                    if archive:
                        archive.close()
                    target = session / f"comparison-{len(archives) + 1:03d}.zip"
                    # Refuse overwriting a prior report accidentally.
                    archive = zipfile.ZipFile(target, "x", compression=zipfile.ZIP_DEFLATED)
                    archives.append(target)
                    payload = 0
                archive.write(file, file.relative_to(staging))
                payload += size
        finally:
            if archive:
                archive.close()
        for archive in archives:
            if archive.stat().st_size >= 25_000_000:
                raise ValueError(f"Archive exceeds GitHub limit: {archive}")
            print(f"Share {archive} ({archive.stat().st_size / 1_000_000:.1f} MB)")
        return archives


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("world", nargs="?", help="saved world to clone (or select/create a world in-game)")
    parser.add_argument("--userdir", type=Path, default=ROOT, help="source user directory")
    parser.add_argument("--output", type=Path, help="new private session directory (must not exist)")
    parser.add_argument("--binary", type=Path, default=ROOT / "cataclysm-tiles")
    parser.add_argument("--interval", type=int, default=30, help="minimum seconds between action-boundary captures")
    parser.add_argument("--count", type=int, default=5, help="maximum checkpoints")
    parser.add_argument("--package", type=Path, help="package an existing session without starting the game")
    args = parser.parse_args()
    if args.package:
        package(args.package.resolve())
        return
    if not 1 <= args.count <= 100 or not 1 <= args.interval <= 86400:
        parser.error("count must be 1..100; interval must be 1..86400")
    if args.world and (Path(args.world).name != args.world or args.world in (".", "..")):
        parser.error("world must be a single directory name")
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"build the tiles executable first: {binary}")
    for tileset in ("Ultica_iso", "UltimateCataclysm"):
        if not (ROOT / "gfx" / tileset / "tile_config.json").is_file():
            parser.error(f"missing {tileset}; run tools/r3d_spike/compose_tilesets.sh {tileset}")
    session = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="r3d-session-"))
    if args.output:
        session.mkdir(parents=True, exist_ok=False)
    user = session / "user"
    (user / "config").mkdir(parents=True)
    (session / "captures").mkdir()
    for name in CONFIG_FILES:
        src = args.userdir / "config" / name
        if src.is_file() and not src.is_symlink():
            shutil.copy2(src, user / "config" / name)
    if args.world:
        source = args.userdir / "save" / args.world
        if not source.is_dir():
            parser.error(f"world does not exist: {source}")
        copy_tree(source, user / "save" / args.world)
    command = [str(binary), "--userdir", str(user) + os.sep]
    if args.world:
        command += ["--world", args.world]
    env = os.environ.copy()
    # Inherited one-shot automation must never terminate a live session or send inputs.
    for name in ("CATA_R3D_COMPARE", "CATA_R3D_QUIT_AFTER_SHOT", "CATA_R3D_SCREENSHOT"):
        env.pop(name, None)
    env.update(CATA_R3D_COMPARE_SESSION=str(session / "captures"),
               CATA_R3D_COMPARE_INTERVAL=str(args.interval), CATA_R3D_COMPARE_COUNT=str(args.count),
               CATA_R3D_CAMERA="-45,30,20,1", CATA_R3D_COMPARE_TILESET="Ultica_iso",
               CATA_R3D_COMPARE_3D_TILESET="UltimateCataclysm")
    (session / "session.json").write_text(json.dumps(dict(
        command=command, interval=args.interval, count=args.count,
        source_world=args.world, private_userdir=str(user)), indent=2))
    print(f"Play normally; captures briefly pause at normal input boundaries.\n"
          f"Your session and saves persist at {session}\nQuit normally to package the report.", flush=True)
    with (session / "stdout.log").open("w") as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    print(f"Game exited with status {result.returncode}. Private saves remain at {user}")
    package(session)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(str(error))
