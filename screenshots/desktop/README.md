# Portable desktop UI previews

Captured from the actual Qt Widgets classes on the development Mac, with fixture
values and no connected device/network/keyring. These are not Windows/Linux VM
screenshots and do not certify target-platform hardware behavior.

The Windows and Linux application uses the same widget geometry; platform fonts,
title bars, tray integration and scaling may differ. Native target-platform CI
captures are configured separately in `host-binaries.yml`.

```sh
python -m pip install ".[desktop]"
QT_QPA_PLATFORM=offscreen python -m scripts.capture_desktop_ui
```

- `usb-light-preview.png`: USB panel.
- `bluetooth-light-preview.png`: Bluetooth panel, light.
- `bluetooth-dark-preview.png`: Bluetooth panel, dark.
- `pairings-dark-preview.png`: recognition management.
- `bluetooth-consent-preview.png`: per-connection Allow Once / Deny.
- `pairing-code-preview.png`: separate six-digit recognition step.
