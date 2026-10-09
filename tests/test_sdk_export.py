from pathlib import Path
import tempfile
import unittest

from scripts.vendor_bridge_sdk import ROOT_FILES, SDK_FILES, export_sdk


class SDKExportTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[1]

    def test_complete_export_with_pairing_and_ui(self):
        with tempfile.TemporaryDirectory(prefix="fib-sdk-test-") as folder:
            destination = Path(folder) / "project with spaces/vendor/internet_bridge"
            export_sdk(self.root, destination)
            for relative in (*ROOT_FILES, *("sdk/flipper/" + name for name in SDK_FILES)):
                self.assertEqual((destination / relative).read_bytes(), (self.root / relative).read_bytes())
            self.assertTrue((destination / "ble_pairing.c").is_file())
            self.assertTrue((destination / "ble_pairing_storage.c").is_file())
            self.assertFalse((destination / "usb_internet_bridge.c").exists())
            self.assertFalse((destination / "application.fam").exists())

    def test_existing_directory_and_symlinks_are_never_overwritten(self):
        with tempfile.TemporaryDirectory(prefix="fib-sdk-test-") as folder:
            directory = Path(folder) / "existing"
            directory.mkdir()
            with self.assertRaises(ValueError):
                export_sdk(self.root, directory)
            self.assertEqual(list(directory.iterdir()), [])
            for name, target in (("live-link", directory), ("dangling-link", Path(folder) / "absent")):
                link = Path(folder) / name
                try:
                    link.symlink_to(target, target_is_directory=True)
                except (OSError, NotImplementedError):
                    self.skipTest("This host does not permit creation of test symlinks")
                with self.assertRaises(ValueError):
                    export_sdk(self.root, link)
                self.assertTrue(link.is_symlink())
            self.assertFalse((Path(folder) / "absent").exists())

    def test_missing_sources_fail_before_creating_destination(self):
        with tempfile.TemporaryDirectory(prefix="fib-sdk-test-") as folder:
            destination = Path(folder) / "output"
            with self.assertRaises(ValueError):
                export_sdk(Path(folder), destination)
            self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
