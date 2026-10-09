"""Compact Qt Widgets counterpart of the native SwiftUI connection panel.

No browser/WebView, local web server or alternate network implementation. All
worker-to-widget delivery uses a queued Qt signal; consent stays explicit.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PySide6.QtCore import QObject, QLockFile, QRegularExpression, Qt, Signal, Slot, QTimer
from PySide6.QtGui import QColor, QIcon, QPainter, QPalette, QRegularExpressionValidator
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QDialog, QFrame, QHBoxLayout, QLabel, QLineEdit,
    QMenu, QMessageBox, QPushButton, QScrollArea, QSystemTrayIcon,
    QTextEdit, QVBoxLayout, QWidget,
)

from .desktop_backend import ConnectionSnapshot, DesktopController, display_text
from .permissions import default_permission_path
from .session import PermissionDecision


def app_icon():
    return QIcon(str(Path(__file__).resolve().parents[1] / "assets" / "fib-bridge.png"))


def label(text="", object_name="", wrap=False):
    widget = QLabel(text)
    widget.setTextFormat(Qt.TextFormat.PlainText)
    widget.setWordWrap(wrap)
    if object_name: widget.setObjectName(object_name)
    return widget


def divider():
    line = QFrame()
    line.setObjectName("divider")
    line.setFixedHeight(1)
    return line


def apply_style(app, dark=False):
    # Fusion is shipped by Qt on both platforms. Same component geometry;
    # system fonts remain native rather than pretending to be SwiftUI.
    app.setStyle("Fusion")
    foreground, secondary = ("#eeeeef", "#a6a6ad") if dark else ("#23232a", "#77777f")
    background, control, border = ("#252528", "#353539", "#49494d") if dark else ("#f7f7f9", "#ffffff", "#d9d9df")
    app.setStyleSheet(f"""
        QWidget {{ color: {foreground}; background: {background}; font-size: 13px; }}
        QLabel#title {{ font-size: 16px; font-weight: 600; }}
        QLabel#secondary {{ color: {secondary}; font-size: 12px; }}
        QLabel#error {{ color: {'#ff9898' if dark else '#b43737'}; font-size: 12px; }}
        QLabel#preview {{ color: {secondary}; font-size: 11px; }}
        QFrame#divider {{ background: {border}; }}
        QPushButton {{ background: {control}; border: 1px solid {border};
                      border-radius: 6px; padding: 6px 10px; }}
        QPushButton:hover {{ border-color: {secondary}; }}
        QPushButton:disabled {{ color: {secondary}; background: {background}; }}
        QPushButton#segment {{ min-height: 16px; border-radius: 5px; }}
        QPushButton#segment:checked {{ background: {'#56565d' if dark else '#e1e1e7'}; font-weight: 600; }}
        QCheckBox {{ spacing: 9px; font-weight: 500; }}
        QCheckBox::indicator {{ width: 30px; height: 16px; border-radius: 8px;
                                background: {border}; border: 1px solid {border}; }}
        QCheckBox::indicator:checked {{ background: #468875; border-color: #468875; }}
        QLineEdit, QTextEdit {{ background: {control}; border: 1px solid {border};
                              border-radius: 6px; padding: 7px; selection-background-color: #468875; }}
        QLineEdit#code {{ font-size: 24px; letter-spacing: 5px; }}
        QScrollArea {{ border: none; }}
    """)


class Observer(QObject):
    changed = Signal()


class BridgeSwitch(QCheckBox):
    """Accessible checkbox behavior, with a small, unanimated switch indicator."""
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        y = (self.height() - 18) // 2
        color = QColor("#468875") if self.isChecked() else self.palette().color(QPalette.ColorRole.Mid)
        if not self.isEnabled(): color.setAlpha(140)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(color)
        painter.drawRoundedRect(0, y, 32, 18, 9, 9)
        painter.setBrush(QColor("#ffffff"))
        painter.drawEllipse(17 if self.isChecked() else 3, y + 3, 12, 12)
        role = QPalette.ColorGroup.Active if self.isEnabled() else QPalette.ColorGroup.Disabled
        painter.setPen(self.palette().color(role, QPalette.ColorRole.WindowText))
        painter.drawText(42, 0, self.width() - 42, self.height(),
                         Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, self.text())
        if self.hasFocus():
            painter.setPen(self.palette().color(QPalette.ColorRole.Highlight))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRoundedRect(0, y - 2, 34, 22, 10, 10)


class ConsentDialog(QDialog):
    def __init__(self, item, parent):
        super().__init__(parent)
        self.item, self.answer = item, None
        self.setWindowTitle("Flipper Internet Bridge")
        self.setWindowModality(Qt.WindowModality.ApplicationModal)
        self.setMinimumWidth(380)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(22, 22, 22, 22)
        layout.setSpacing(14)
        code = item.kind == "code"
        layout.addWidget(label(f"Pair with {item.name}" if code else "Flipper Zero is requesting internet access", "title", True))
        message = ("Enter the six-digit bridge code shown on your Flipper. Pairing identifies this computer; it does not grant internet access."
            if code else f"{item.name} wants to send HTTPS requests through this computer. Your Wi-Fi password and browser cookies are never shared."
            + (" Bluetooth access lasts only for this connection." if item.link == "bluetooth" else ""))
        layout.addWidget(label(message, "secondary", True))
        buttons = QHBoxLayout()
        deny = QPushButton("Cancel" if code else "Deny")
        deny.clicked.connect(self.reject)
        buttons.addWidget(deny)
        if code:
            self.code = QLineEdit()
            self.code.setObjectName("code")
            self.code.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.code.setMaxLength(6)
            self.code.setPlaceholderText("000000")
            self.code.setValidator(QRegularExpressionValidator(QRegularExpression("[0-9]{0,6}"), self))
            layout.addWidget(self.code)
            pair = QPushButton("Pair")
            pair.setEnabled(False)
            self.code.textChanged.connect(lambda text: pair.setEnabled(len(text) == 6))
            pair.clicked.connect(lambda: self.choose(self.code.text()))
            self.code.returnPressed.connect(lambda: self.choose(self.code.text()) if len(self.code.text()) == 6 else None)
            buttons.addWidget(pair)
        else:
            once = QPushButton("Allow Once")
            once.clicked.connect(lambda: self.choose(PermissionDecision.ALLOW_ONCE))
            buttons.addWidget(once)
            if item.link == "usb":
                always = QPushButton("Always Allow")
                always.clicked.connect(lambda: self.choose(PermissionDecision.ALLOW_ALWAYS))
                buttons.addWidget(always)
        layout.addLayout(buttons)
        # No Return-to-Allow shortcut. Closing a dialog is denial/cancellation.
        for button in self.findChildren(QPushButton): button.setAutoDefault(False)

    def choose(self, answer):
        self.answer = answer
        self.accept()


class PairingsWindow(QWidget):
    def __init__(self, controller, parent=None):
        super().__init__(parent, Qt.WindowType.Window)
        self.controller = controller
        self.setWindowTitle("Bluetooth Pairings")
        self.resize(420, 300)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(12)
        layout.addWidget(label("Flippers recognized by this bridge", "title", True))
        layout.addWidget(label("Revoke Pairing forgets bridge recognition, not USB internet permission or system Bluetooth bonds.", "secondary", True))
        self.area = QScrollArea()
        self.area.setWidgetResizable(True)
        layout.addWidget(self.area, 1)
        self.error = label("", "error", True)
        layout.addWidget(self.error)
        self._records = None

    def refresh(self):
        self.error.setText(self.controller.pairing_error)
        records = self.controller.records
        if records == self._records: return
        self._records = records
        body = QWidget()
        layout = QVBoxLayout(body)
        layout.setContentsMargins(0, 2, 0, 2)
        if not records:
            layout.addWidget(label("No recognized Flippers yet. Select this computer in Flipper → Bluetooth Internet Bridge → Connection Requests.", "secondary", True))
        for key, name in records:
            row = QHBoxLayout()
            info = QVBoxLayout()
            info.addWidget(label(display_text(name, 32)))
            info.addWidget(label(f"Bridge ID …{key[-8:].upper()}", "secondary"))
            row.addLayout(info, 1)
            revoke = QPushButton("Revoke Pairing…")
            revoke.clicked.connect(lambda _checked=False, k=key, n=name: self.revoke(k, n))
            row.addWidget(revoke)
            layout.addLayout(row)
        layout.addStretch()
        old = self.area.takeWidget()
        self.area.setWidget(body)
        if old: old.deleteLater()

    def revoke(self, key, name):
        dialog = QMessageBox(self)
        dialog.setWindowTitle("Revoke Pairing")
        dialog.setTextFormat(Qt.TextFormat.PlainText)
        dialog.setText(f"Revoke pairing with {display_text(name, 32)}?")
        dialog.setInformativeText("A new bridge code is required next time. USB permissions and system bonds are unchanged.")
        dialog.setStandardButtons(QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Ok)
        dialog.setDefaultButton(QMessageBox.StandardButton.Cancel)
        if dialog.exec() == QMessageBox.StandardButton.Ok: self.controller.revoke_pairing(key)


class DiagnosticsWindow(QWidget):
    def __init__(self, controller, parent=None):
        super().__init__(parent, Qt.WindowType.Window)
        self.controller = controller
        self.setWindowTitle("Flipper Internet Bridge Diagnostics")
        self.resize(680, 420)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.addWidget(label("Diagnostics", "title"))
        layout.addWidget(label("Request bodies, header values, pairing codes and tokens are not logged.", "secondary", True))
        self.text = QTextEdit()
        self.text.setReadOnly(True)
        layout.addWidget(self.text)
        self._last = ()

    def refresh(self):
        with self.controller._lock: entries = tuple(self.controller.logs)
        if entries != self._last:
            self._last = entries
            self.text.setPlainText("\n".join(f"{stamp}  {link:9}  {message}" for stamp, link, message in entries))


class BridgeWindow(QWidget):
    def __init__(self, controller, preview=False, tray=True):
        super().__init__()
        self.controller, self.preview = controller, preview
        self.link = "usb"
        self._dialog = None
        self._quitting = False
        self.setWindowTitle("Flipper Internet Bridge")
        self.setWindowIcon(app_icon())
        self.setFixedWidth(340)
        self.observer = Observer(self)
        self.observer.changed.connect(self.refresh, Qt.ConnectionType.QueuedConnection)
        controller.on_change = self.observer.changed.emit
        self.pairings = PairingsWindow(controller, self)
        self.diagnostics = DiagnosticsWindow(controller, self)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(18, 18, 18, 18)
        layout.setSpacing(14)
        heading = QHBoxLayout()
        heading.addWidget(label("Flipper Internet Bridge", "title"), 1)
        icon = QLabel()
        icon.setPixmap(app_icon().pixmap(24, 24))
        heading.addWidget(icon)
        layout.addLayout(heading)
        tabs = QHBoxLayout()
        tabs.setSpacing(3)
        self.tabs = {}
        for link, name in (("usb", "USB"), ("bluetooth", "Bluetooth")):
            button = QPushButton(name)
            button.setObjectName("segment")
            button.setCheckable(True)
            button.clicked.connect(lambda _checked=False, selected=link: self.select(selected))
            tabs.addWidget(button, 1)
            self.tabs[link] = button
        layout.addLayout(tabs)
        self.enabled = BridgeSwitch()
        self.enabled.toggled.connect(lambda enabled: controller.set_enabled(self.link, enabled))
        layout.addWidget(self.enabled)
        status = QHBoxLayout()
        self.dot = label("●")
        self.connection = label("", wrap=True)
        status.addWidget(self.dot)
        status.addWidget(self.connection, 1)
        layout.addLayout(status)
        self.identity = label("", "secondary", True)
        self.status = label("", "secondary", True)
        self.error = label("", "error", True)
        layout.addWidget(self.identity)
        layout.addWidget(self.status)
        layout.addWidget(self.error)
        controls = QHBoxLayout()
        self.allow = QPushButton()
        self.allow.clicked.connect(lambda: controller.command(self.link, "allow"))
        self.revoke = QPushButton("Revoke Access")
        self.revoke.clicked.connect(lambda: controller.command(self.link, "revoke"))
        controls.addWidget(self.allow)
        controls.addWidget(self.revoke)
        layout.addLayout(controls)
        self.cancel = QPushButton("Cancel Request")
        self.cancel.clicked.connect(lambda: controller.command(self.link, "cancel"))
        layout.addWidget(self.cancel)
        self.ble_controls = QWidget()
        ble_row = QHBoxLayout(self.ble_controls)
        ble_row.setContentsMargins(0, 0, 0, 0)
        pairings = QPushButton("Pairings…")
        pairings.clicked.connect(self.show_pairings)
        self.reconnect = QPushButton("Reconnect")
        self.reconnect.clicked.connect(lambda: controller.set_enabled("bluetooth", True))
        ble_row.addWidget(pairings)
        ble_row.addStretch()
        ble_row.addWidget(self.reconnect)
        layout.addWidget(self.ble_controls)
        self.hint = label("", "secondary", True)
        layout.addWidget(self.hint)
        layout.addWidget(divider())
        footer = QHBoxLayout()
        diagnostics = QPushButton("Diagnostics")
        diagnostics.clicked.connect(self.show_diagnostics)
        quit_button = QPushButton("Quit")
        quit_button.clicked.connect(self.quit)
        footer.addWidget(diagnostics)
        footer.addStretch()
        footer.addWidget(quit_button)
        layout.addLayout(footer)
        if preview:
            layout.addWidget(label("UI preview · no device connection", "preview"))
            self.enabled.setEnabled(False)
        self.tray = None
        if tray and not preview and QSystemTrayIcon.isSystemTrayAvailable():
            self.tray = QSystemTrayIcon(app_icon(), self)
            self.tray.setToolTip("Flipper Internet Bridge")
            menu = QMenu()
            menu.addAction("Open Bridge", self.show_front)
            menu.addAction("Diagnostics", self.show_diagnostics)
            menu.addSeparator()
            menu.addAction("Quit", self.quit)
            self.tray.setContextMenu(menu)
            self.tray.activated.connect(lambda reason: self.show_front() if reason == QSystemTrayIcon.ActivationReason.Trigger else None)
            self.tray.show()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh)
        self.timer.start(200)
        self.refresh()

    def select(self, link):
        # Presentation only: switching tabs does not stop either connection.
        self.link = link
        self.refresh()

    @Slot()
    def refresh(self):
        if self._quitting: return
        snapshot = self.controller.snapshot(self.link)
        bluetooth = self.link == "bluetooth"
        for link, tab in self.tabs.items(): tab.setChecked(link == self.link)
        self.enabled.blockSignals(True)
        self.enabled.setChecked(snapshot.enabled)
        self.enabled.setText("Bluetooth Internet Bridge" if bluetooth else "USB Internet Bridge")
        self.enabled.blockSignals(False)
        self.connection.setText("Flipper connected" if snapshot.connected else "Flipper not connected")
        self.dot.setStyleSheet("color: #468875;" if snapshot.access else "color: #99999f;")
        self.identity.setText(f"{snapshot.name}\n{snapshot.identity}" if snapshot.connected else snapshot.identity)
        self.status.setText(snapshot.status)
        self.error.setText(snapshot.error)
        self.error.setVisible(bool(snapshot.error))
        self.allow.setText("Allow Once" if bluetooth else "Always Allow")
        self.allow.setEnabled(snapshot.enabled and snapshot.connected and not snapshot.access and not self.preview)
        self.revoke.setEnabled(snapshot.access and not self.preview)
        self.cancel.setVisible(snapshot.active_request)
        self.ble_controls.setVisible(bluetooth)
        self.reconnect.setEnabled(snapshot.enabled and not self.preview)
        self.hint.setText("Select this computer in Flipper → Connection Requests. Pairing and internet permission are separate."
            if bluetooth else "Connect by USB, then choose USB Internet Bridge on Flipper. Internet Radio uses this connection.")
        self.pairings.refresh()
        self.diagnostics.refresh()
        self.sync_prompt()
        self.adjustSize()

    def sync_prompt(self):
        pending = self.controller.prompts.pending()
        if self._dialog and not any(item.id == self._dialog.item.id for item in pending):
            self._dialog.reject()
        if self._dialog is not None or not pending: return
        item = pending[0]
        dialog = ConsentDialog(item, self)
        self._dialog = dialog
        def done(_result):
            self.controller.prompts.answer(item.id, dialog.answer)
            if self._dialog is dialog: self._dialog = None
            dialog.deleteLater()
        dialog.finished.connect(done)
        dialog.show()
        dialog.raise_()
        dialog.activateWindow()

    def show_pairings(self):
        if not self.preview: self.controller.refresh_pairings()
        self.pairings.refresh()
        self.pairings.show()
        self.pairings.raise_()

    def show_diagnostics(self):
        self.diagnostics.refresh()
        self.diagnostics.show()
        self.diagnostics.raise_()

    def show_front(self):
        self.show()
        self.raise_()
        self.activateWindow()

    def closeEvent(self, event):
        if self.tray is not None and not self._quitting:
            event.ignore()
            self.hide()
        else:
            self.quit()
            event.accept()

    def quit(self):
        if self._quitting: return
        self._quitting = True
        self.timer.stop()
        self.controller.close()
        if self.tray: self.tray.hide()
        QApplication.instance().quit()


def main(argv=None):
    parser = argparse.ArgumentParser(description="Flipper Internet Bridge desktop helper")
    parser.add_argument("--dark", action="store_true", help="use the dark compact panel")
    args = parser.parse_args(argv)
    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setApplicationName("Flipper Internet Bridge")
    app.setWindowIcon(app_icon())
    root = default_permission_path().parent
    root.mkdir(parents=True, exist_ok=True)
    lock = QLockFile(str(root / "desktop.lock"))
    lock.setStaleLockTime(10_000)
    if not lock.tryLock(0):
        QMessageBox.information(None, "Flipper Internet Bridge", "The desktop bridge is already running. Open it from the system tray.")
        return 1
    apply_style(app, args.dark)
    controller = DesktopController()
    window = BridgeWindow(controller)
    app.aboutToQuit.connect(controller.close)
    controller.set_enabled("usb", True)
    window.show()
    try: return app.exec()
    finally:
        controller.close()
        lock.unlock()


if __name__ == "__main__":
    raise SystemExit(main())
