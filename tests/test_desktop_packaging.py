"""Installer filesystem checks in a temporary XDG directory, no actual install."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(os.name == "posix", "shell installer is Linux/POSIX-only")
class DesktopInstallerTests(unittest.TestCase):
    def test_graphical_launcher_recoverable_update_and_uninstall(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="fib-installer-test-") as temp:
            directory = Path(temp)
            package = directory / "package with spaces"
            package.mkdir()
            gui = package / "fib-bridge-desktop"
            gui.mkdir()
            (gui / "fib-bridge-desktop").write_text("fixture, not an executable\n")
            (package / "fib-bridge").write_text("fixture CLI\n")
            shutil.copy2(root / "host/assets/fib-bridge.png", package / "fib-bridge.png")
            env = dict(os.environ, XDG_DATA_HOME=str(directory / "data with spaces"))
            data = Path(env["XDG_DATA_HOME"])
            def run(name):
                shutil.copy2(root / "host/linux" / name, package / name)
                return subprocess.run(["sh", str(package / name)], env=env, check=True, capture_output=True, text=True).stdout
            run("install.sh")
            desktop = data / "applications/flipper-usb-internet-bridge.desktop"
            self.assertIn("Terminal=false", desktop.read_text())
            self.assertIn('Exec="', desktop.read_text())
            installed = data / "flipper-usb-internet-bridge"
            (installed / "fib-bridge-desktop/keep.txt").write_text("old GUI content")
            self.assertIn("preserved", run("install.sh"))
            backups = list(installed.glob("previous-gui.*/fib-bridge-desktop/keep.txt"))
            self.assertEqual(len(backups), 1)
            self.assertIn("preserved", run("uninstall.sh"))
            self.assertFalse(desktop.exists())
            self.assertEqual(len(list(installed.glob("removed-gui.*/fib-bridge-desktop"))), 1)


if __name__ == "__main__": unittest.main()
