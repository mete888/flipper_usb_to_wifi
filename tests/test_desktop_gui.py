"""Optional actual-widget tests. Offscreen, no device/network/keyring access."""
from __future__ import annotations
import os
import unittest
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
try:
    from PySide6.QtWidgets import QApplication, QPushButton
    from host.fibp_host.desktop_gui import BridgeWindow, ConsentDialog, apply_style
    QT_AVAILABLE = True
except ImportError:
    QT_AVAILABLE = False
from host.fibp_host.desktop_backend import ConnectionSnapshot, DesktopController, PromptBroker
from host.fibp_host.session import PermissionDecision


@unittest.skipUnless(QT_AVAILABLE, "Install the desktop extra for Qt widget tests")
class DesktopWidgetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        apply_style(cls.app)

    def setUp(self):
        self.controller = DesktopController(port_provider=lambda: [])
        self.window = BridgeWindow(self.controller, preview=True, tray=False)
        self.addCleanup(self.controller.close)
        self.addCleanup(self.window.timer.stop)

    def test_tabs_are_presentation_only_and_independent(self):
        usb = ConnectionSnapshot("usb", True, True, "Mico", "Device", "Internet access ready", True)
        self.controller.snapshots["usb"] = usb
        self.window.select("bluetooth")
        self.assertEqual(self.controller.snapshot("usb"), usb)
        self.assertEqual(self.window.allow.text(), "Allow Once")
        self.window.select("usb")
        self.assertEqual(self.window.allow.text(), "Always Allow")

    def test_bluetooth_prompt_has_no_always_allow(self):
        broker = PromptBroker()
        item = broker.request("test", "bluetooth", "permission", "Mico")
        dialog = ConsentDialog(item, self.window)
        buttons = {button.text() for button in dialog.findChildren(QPushButton)}
        self.assertEqual(buttons, {"Deny", "Allow Once"})
        dialog.choose(PermissionDecision.ALLOW_ONCE)
        self.assertEqual(dialog.answer, PermissionDecision.ALLOW_ONCE)

    def test_code_requires_six_ascii_digits(self):
        item = PromptBroker().request("test", "bluetooth", "code", "Mico")
        dialog = ConsentDialog(item, self.window)
        pair = next(button for button in dialog.findChildren(QPushButton) if button.text() == "Pair")
        self.assertFalse(pair.isEnabled())
        dialog.code.setText("12345")
        self.assertFalse(pair.isEnabled())
        dialog.code.setText("123456")
        self.assertTrue(pair.isEnabled())
        self.assertTrue(dialog.code.hasAcceptableInput())

    def test_disconnected_state_disables_access_controls(self):
        self.window.refresh()
        self.assertFalse(self.window.allow.isEnabled())
        self.assertFalse(self.window.revoke.isEnabled())
        self.assertFalse(self.window.cancel.isVisible())

    def test_empty_pairings_has_no_ghost_rows(self):
        self.controller.records = (("a" * 64, "Mico"),)
        self.window.pairings.refresh()
        self.assertEqual(len(self.window.pairings.area.widget().findChildren(QPushButton)), 1)
        self.controller.records = ()
        self.window.pairings.refresh()
        self.assertEqual(len(self.window.pairings.area.widget().findChildren(QPushButton)), 0)


if __name__ == "__main__": unittest.main()
