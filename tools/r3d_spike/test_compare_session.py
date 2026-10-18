#!/usr/bin/env python3
"""Packaging checks, including large saves and filesystem boundaries."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import zipfile

spec = importlib.util.spec_from_file_location("compare_session", Path(__file__).with_name("compare_session.py"))
session_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session_tool)


class SessionPackagingTest(unittest.TestCase):
    def test_packaging_round_trip_and_allowlist(self):
        with tempfile.TemporaryDirectory() as temporary:
            session = Path(temporary)
            capture = session / "captures" / "capture-0001"
            world = capture / "save" / "World"
            world.mkdir(parents=True)
            (capture / "COMPLETE.txt").write_text("ok")
            (capture / "view_2d.png").write_bytes(b"image")
            data = bytes(range(256)) * 85000  # exceeds the individual-file chunk threshold
            (world / "big-save.zzip").write_bytes(data)
            (world / "second-save.zzip").write_bytes(data[:10_000_000])
            failed = session / "captures" / "capture-0002"
            failed.mkdir()
            (failed / "FAILED.txt").write_text("bad")
            (failed / "private.txt").write_text("exclude")
            (session / "unrelated.txt").write_text("exclude")
            config = session / "user" / "config"
            config.mkdir(parents=True)
            (config / "debug.log").write_text("debug")
            (config / "secret.json").write_text("exclude")
            with contextlib.redirect_stdout(io.StringIO()):
                archives = session_tool.package(session)
            self.assertGreater(len(archives), 1)
            restored = session / "restored"
            restored.mkdir()
            for archive in archives:
                self.assertLess(archive.stat().st_size, 25_000_000)
                with zipfile.ZipFile(archive) as zipped:
                    self.assertIsNone(zipped.testzip())
                    zipped.extractall(restored)
            subprocess.run(["python3", str(restored / "restore.py")], check=True, capture_output=True)
            self.assertEqual((restored / "captures/capture-0001/save/World/big-save.zzip").read_bytes(), data)
            self.assertFalse((restored / "captures/capture-0002").exists())
            self.assertFalse((restored / "unrelated.txt").exists())
            self.assertFalse((restored / "diagnostics/secret.json").exists())
            self.assertEqual((restored / "diagnostics/debug.log").read_text(), "debug")
            self.assertEqual(len(json.loads((restored / "large-files.json").read_text())), 1)
            with self.assertRaises(FileExistsError), contextlib.redirect_stdout(io.StringIO()):
                session_tool.package(session)

    def test_restore_rejects_traversal_symlinks_and_existing_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / "report"
            report.mkdir()
            (report / "restore.py").write_text(session_tool.RESTORE)
            (report / "chunk.bin").write_bytes(b"content")
            (report / "existing.txt").write_text("keep")
            (report / "link").symlink_to(root, target_is_directory=True)
            for target, part in (("../outside.txt", "chunk.bin"),
                                 (str(root / "absolute.txt"), "chunk.bin"),
                                 ("link/escape.txt", "chunk.bin"),
                                 ("existing.txt", "chunk.bin"),
                                 ("safe.txt", "../outside-chunk.bin")):
                manifest = [dict(path=target, parts=[part], sha256="unused")]
                (report / "large-files.json").write_text(json.dumps(manifest))
                result = subprocess.run(["python3", str(report / "restore.py")], capture_output=True)
                self.assertNotEqual(result.returncode, 0, target)
            self.assertEqual((report / "existing.txt").read_text(), "keep")
            self.assertFalse((root / "outside.txt").exists())
            self.assertFalse((root / "absolute.txt").exists())
            self.assertFalse((root / "escape.txt").exists())
            self.assertFalse((report / "safe.txt").exists())

    def test_refuse_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            (source / "escape").symlink_to(root)
            with self.assertRaises(ValueError):
                session_tool.copy_tree(source, root / "target")

    def test_no_stale_save_archive_after_failed_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "captures/capture-0001"
            capture.mkdir(parents=True)
            (capture / "FAILED.txt").write_text("save failed")
            with self.assertRaisesRegex(ValueError, "No complete checkpoints"):
                session_tool.package(Path(temporary))
            self.assertFalse(list(Path(temporary).glob("*.zip")))


class SessionLaunchTest(unittest.TestCase):
    def test_preflight_rejects_old_binary_and_requires_metal_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shaders = root / "data/shaders"
            shaders.mkdir(parents=True)
            for name in ("r3d_block.vert.spv", "r3d_block.frag.spv"):
                (shaders / name).write_bytes(b"shader")
            result = subprocess.CompletedProcess([], 0, "old build", "")
            with mock.patch.object(session_tool, "ROOT", root), \
                 mock.patch.object(session_tool.subprocess, "run", return_value=result):
                with self.assertRaisesRegex(ValueError, "Rebuild the tiles"):
                    session_tool.preflight(root / "game", {}, "darwin")
                result.stdout = "build\nRenderer capture protocol: 1\n"
                with self.assertRaisesRegex(ValueError, "BUILD_SHADER_FORMATS=msl"):
                    session_tool.preflight(root / "game", {}, "darwin")
                session_tool.preflight(root / "game", {"SDL_GPU_DRIVER": "vulkan"}, "darwin")
                for name in ("r3d_block.vert.msl", "r3d_block.frag.msl"):
                    (shaders / name).write_bytes(b"metal shader")
                session_tool.preflight(root / "game", {}, "darwin")

    def test_launcher_isolates_options_and_clears_inherited_automation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            (source / "config").mkdir(parents=True)
            original = '[{"name": "USE_TILES", "value": "false"}]'
            (source / "config/options.json").write_text(original)
            binary = root / "fake-game"
            binary.write_text('''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
if "--version" in sys.argv:
    print("Renderer capture protocol: 1")
    sys.exit(0)
assert os.environ["SDL_RENDER_DRIVER"] == "gpu"
for key in ("CATA_R3D_COMPARE", "CATA_R3D_QUIT_AFTER_SHOT", "CATA_R3D_SCREENSHOT"):
    assert key not in os.environ
user = Path(sys.argv[sys.argv.index("--userdir") + 1])
assert json.loads((user / "config/options.json").read_text()) == [{"name": "USE_TILES", "value": "true"}]
assert not (user / "save").exists()
captures = Path(os.environ["CATA_R3D_COMPARE_SESSION"])
(captures / "session-status.txt").write_text("Capture support initialized; waiting for a character.")
''')
            binary.chmod(0o755)
            checkout = root / "checkout"
            script = checkout / "tools/r3d_spike/compare_session.py"
            script.parent.mkdir(parents=True)
            script.write_bytes(Path(session_tool.__file__).read_bytes())
            for tileset in ("Ultica_iso", "UltimateCataclysm"):
                config = checkout / "gfx" / tileset / "tile_config.json"
                config.parent.mkdir(parents=True)
                config.write_text("{}")
            shaders = checkout / "data/shaders"
            shaders.mkdir(parents=True)
            for suffix in ("spv", "msl", "dxil"):
                for name in ("r3d_block.vert", "r3d_block.frag"):
                    (shaders / f"{name}.{suffix}").write_bytes(b"shader")
            output = root / "session"
            env = dict(os.environ, SDL_RENDER_DRIVER="software", CATA_R3D_COMPARE="stale",
                       CATA_R3D_QUIT_AFTER_SHOT="1", CATA_R3D_SCREENSHOT="stale")
            result = subprocess.run(["python3", str(script),
                                     "--binary", str(binary), "--userdir", str(source),
                                     "--output", str(output)], env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("waiting for a character", result.stderr)
            self.assertIn("starts without existing worlds", result.stdout)
            self.assertEqual((source / "config/options.json").read_text(), original)
            self.assertFalse(list(output.glob("*.zip")))

    def test_failure_diagnostics_report_actual_failed_stage(self):
        with tempfile.TemporaryDirectory() as temporary:
            session = Path(temporary)
            capture = session / "captures/capture-0001"
            capture.mkdir(parents=True)
            (capture / "FAILED.txt").write_text("Failed stages: 3D renderer or screenshot;")
            config = session / "user/config"
            config.mkdir(parents=True)
            (config / "debug.log").write_text(
                "ERROR : r3d: SDL gpu renderer required; active renderer: software\n" +
                "INFO : r3d compare: saved screenshot\n" * 30)
            with self.assertRaisesRegex(ValueError, "capture-0001: Failed stages: 3D renderer"):
                session_tool.package(session)
            self.assertIn("active renderer: software", session_tool.failure_details(session))


if __name__ == "__main__":
    unittest.main()
