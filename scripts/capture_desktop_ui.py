"""Render real Qt widgets without a device or credentials.

These are portable UI previews rendered on the current OS, NOT screenshots
from a Windows/Linux VM. Fixture values are labelled in every main-panel image.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("screenshots/desktop"))
    args = parser.parse_args()
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PySide6.QtWidgets import QApplication
    from host.fibp_host.desktop_backend import ConnectionSnapshot, DesktopController
    from host.fibp_host.desktop_gui import BridgeWindow, ConsentDialog, apply_style
    from host.fibp_host.session import PermissionDecision
    app = QApplication([])
    controller = DesktopController(port_provider=lambda: [])
    controller.snapshots["usb"] = ConnectionSnapshot("usb", True, True, "Mico",
        "Device: Flipper Zero · ID …12345678", "Internet access ready", True)
    controller.snapshots["bluetooth"] = ConnectionSnapshot("bluetooth", True, True, "Mico",
        "Device: Flipper Zero · ID …12345678", "Internet access ready", True)
    controller.records = (("a" * 64, "Mico"),)
    window = BridgeWindow(controller, preview=True, tray=False)
    args.output.mkdir(parents=True, exist_ok=True)
    def save(widget, name):
        widget.show()
        app.processEvents()
        target = args.output / name
        if not widget.grab().save(str(target)): raise RuntimeError("Could not save " + str(target))
        print(target)
    apply_style(app, False)
    save(window, "usb-light-preview.png")
    window.select("bluetooth")
    save(window, "bluetooth-light-preview.png")
    apply_style(app, True)
    save(window, "bluetooth-dark-preview.png")
    window.pairings.refresh()
    save(window.pairings, "pairings-dark-preview.png")
    # Separate dialogs use the same actual UI classes as live worker prompts.
    controller.on_change = lambda: None
    prompt = controller.prompts.request("fixture", "bluetooth", "permission", "Mico")
    dialog = ConsentDialog(prompt, window)
    save(dialog, "bluetooth-consent-preview.png")
    dialog.hide()
    controller.prompts.answer(prompt.id, PermissionDecision.DENY)
    prompt = controller.prompts.request("fixture", "bluetooth", "code", "Mico")
    dialog = ConsentDialog(prompt, window)
    save(dialog, "pairing-code-preview.png")
    window.timer.stop()
    controller.close()


if __name__ == "__main__":
    main()
