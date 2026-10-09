# Toolbox and desktop UI validation — 2026-10-08

Later National Today fixes and successful physical live-reader checks are
recorded in [the follow-up report](test-results-2026-10-08-national-today.md).
The other pending hardware checks below remain pending.

## Scope

- Wikipedia: measured, padded ASCII reader, no tagline/divider; OK starts a new
  search. National Today: the same reader with inline bold holiday names.
- Weather: location/temperature/condition page and a second details page. ISS:
  latitude/longitude page and altitude/speed page. Arrows change pages; OK refreshes.
- Shared view-model union keeps one tool's data resident, not four new buffers.
  USB radio decoder/ring/gain, transport selection and Bluetooth recognition stay
  unchanged. Radio is still USB-only.
- Optional Windows/Linux Qt Widgets panel follows the native Mac USB/Bluetooth
  structure, with separate transport owners and cancellable modal consent/code
  prompts. CLI remains available; network/protocol implementations are shared.

## Passed locally on macOS

- uFBT FAP build/import check: Target 7, API 87.1.
- 86 Python tests, including real offscreen Qt widget tests, GUI BLE code then
  Allow/Deny with fake GATT/keyring peers, asynchronous USB consent over a real
  POSIX PTY, stale-owner protection and recoverable Linux-installer filesystem
  checks in a temporary XDG directory. Rapid Off/On waits for every still-closing
  serial owner; it cannot open the same port concurrently. No real keyring entries changed by tests.
- Swift: 109 executed, 103 passed, six opt-in live/hardware checks skipped;
  warnings treated as errors, zero failures.
- Nine portable C programs passed ASan/UBSan: protocol, menus, BLE recognition,
  Markets, Toolbox parsing/cards/UI, radio PCM/Stop/restart and timer-wrap checks.
  UI doubles do not certify physical font rendering or the firmware scheduler.
- Real Qt window lifecycle with a USB worker and no connected device passed;
  tab switching did not stop USB or enable Bluetooth; Quit stopped its owner.
- Portable HTTPS client live provider smoke: 16/16 HTTP 200, nonempty bounded
  payloads with no truncation/cancellation. Includes all four refreshed tools,
  radio directory, Markets, earthquakes, currency and dictionary. This does not
  certify streaming radio playback or the native Windows/Linux network path.
- Six previews captured directly from actual Qt widgets, not generated artwork.
  Fixture values are labelled; rendering occurred on macOS, not in target OS VMs.
- macOS release app/DMG built, strict ad-hoc signature verification passed.
  Existing installed app replaced recoverably; bundle ID and executable name
  unchanged. Build staging is `dist/macos/Build.noindex/` to avoid duplicate
  Spotlight apps. Spotlight returned only `/Applications/Flipper Internet Bridge.app`.
- Shell/Python syntax checks and `git diff --check` passed.

## Physical Flipper status

The new FAP was uploaded to `/ext/apps/USB/usb_internet_bridge.fap`, launched,
and the final on-device/local MD5 matched `7e0722ecfc211465adc73db3e78bfe48`
(106,704 bytes). Previous FAP is recoverable under
`.build-tests/fap-before-toolbox-ui-e8tlwbe5/`; previous installed Mac app under
`.build-tests/macos-backups.noindex/installed.t8nm9f/`.

Real display capture confirmed the USB menu. Selecting USB while screen RPC
was open hit the expected firmware USB lock; ending RPC and selecting through
ordinary CLI re-enumerated dual CDC successfully. The lock was not bypassed.
Live National Today/ISS navigation reached the normal waiting-for-host-permission
gate. Mac approval was requested from the user; no persistent grant was inserted
and no permission dialog was approved programmatically. Thus the four new live
tool screens, physical scrolling/refresh and refreshed-build radio playback are
**not yet claimed as hardware passes**.

Final restart confirmed the normal `Internet Bridge` USB/Bluetooth selector
on the physical display. A tiny captured image during remote key testing was
initially misread as "Internal error"; the enlarged real-pixel capture showed
the `Internet Bridge` title behind an empty dolphin popup, not that error text.
The app was running according to `loader info`; restart cleared the popup.
Its underlying cause is not established, so it is not claimed as a fixed
application crash. Final selector capture: `.build-tests/toolbox-final-restarted.png`.
The device is left at this selector; choose USB and Allow Once for live tool QA.

## Target-platform boundary

No Windows/Linux VM or physical host is available in this workspace. The CI
matrix now installs GUI dependencies, tests native widgets and builds a GUI
directory with dependency notices and the existing CLI. It also captures
target-platform widget previews. The modified workflow has not been dispatched
or published. Native `.exe`/Linux package execution, tray behavior, BlueZ/Windows
BLE, system keyring and real-device unplug behavior still require those hosts.
Review dependency distribution obligations before public binary publication.

No Git commit, push, release or Apps Catalog modification was performed.

## Reproduce

```sh
ufbt
python -m pip install ".[desktop]"
QT_QPA_PLATFORM=offscreen python -m unittest discover -s tests -p 'test_*.py' -q
(cd macos && swift test -Xswiftc -warnings-as-errors)
./scripts/package_macos.sh
QT_QPA_PLATFORM=offscreen python -m scripts.capture_desktop_ui
python -m scripts.live_endpoint_smoke --live
```

Use the existing C commands in `.github/workflows/build.yml`. On this Mac the
matched Command Line Tools clang/SDK were used for sanitizer executables.
Radio decoder tests need `FIB_RADIO_FIXTURE` pointing to a **host-only** MP3
fixture; the tone fixture was not played on Flipper. See
[host instructions](../host/README.md) for target-OS package commands.

## Remaining manual checks

1. Allow Once in the normal Mac USB prompt. Open National Today and ISS; check
   bold words, wrapping, arrow navigation, refresh and Back. Capture actual pixels.
2. Search Wikipedia for `Ocean`, Weather for `London`, choose a location, and
   inspect both forecast pages. Test long city/title strings and empty results.
3. Back during fetch; unplug while refreshing; no late completion may reopen a tool.
4. Retest USB radio, Stop/Back and Markets without changing audio output/gain.
5. On Windows/Linux, run GUI and consent/code/cancel/revoke/unplug checks, then
   verify tray close/Quit, serial access and secure-keyring recognition persistence.
