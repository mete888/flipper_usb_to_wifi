# Internet Bridge 0.5.0 release validation

## Rechecked on 2026-10-09

- Refreshed the official Release SDK with uFBT: firmware 1.4.3, Target 7,
  API 87.1. Main FAP and exported standalone SDK consumer build/import checks passed.
- All ten portable C programs in the build workflow passed strict warnings and
  ASan/UBSan using the matched macOS Command Line Tools compiler and SDK.
- Python host, desktop widgets, simulator and SDK export: 91 tests passed.
- Swift with warnings as errors: 111 tests executed, 105 passed, six opt-in
  live/hardware tests skipped; no failures.
- Real portable HTTPS provider checks: all 16 returned HTTP 200 and bounded,
  nonempty expected payloads. This includes radio directory lookup, not playback.
- The native macOS arm64 release app and DMG rebuilt; strict ad-hoc signature
  verification passed. This is not Developer ID signing or notarization.
- The icon is a 10-by-10, 1-bit black/white PNG. The new connection selector
  screenshot was saved directly with qFlipper's Save Screenshot button without
  editing: 512-by-256, exactly its orange/black palette.
- Known credential/token/private-key patterns were checked in publishable
  source files with no matches. No pairing stores or private build directories
  are included. Pattern scanning is not a guarantee against every possible secret.

## Hardware and platform limits

The physical Flipper successfully launched Internet Bridge and displayed the
USB/Bluetooth selector used in the new screenshot. The previously installed main
FAP is the same source build; its firmware is Momentum, not an official-firmware
hardware certification. Build compatibility was checked against the official SDK.

Earlier physical checks and their limits are recorded in the linked reports.
The reusable SDK's live USB GET and complete Bluetooth code/consent/GET/revoke
sequence remain unverified end to end. Native Windows/Linux device, Bluetooth,
keyring and packaged application behavior still require those platforms.
No consent, credential, OS privacy setting or system Bluetooth bond was bypassed.

- [SDK checks and physical evidence](test-results-2026-10-08-sdk.md)
- [Connection modes](connection-modes.md)
- [Toolbox UI checks and pending checks](test-results-2026-10-08-ui.md)
- [National Today live reader checks](test-results-2026-10-08-national-today.md)

## Repeat the checks

From the source repository root, after setting up the development environments:

```sh
./.ufbt-venv/bin/ufbt update --channel release
./.ufbt-venv/bin/ufbt
UFBT="$PWD/.ufbt-venv/bin/ufbt" ./scripts/build_sdk_example.sh
QT_QPA_PLATFORM=offscreen python3 -m unittest discover -s tests -p 'test_*.py' -q
swift test --package-path macos -Xswiftc -warnings-as-errors
python3 -m scripts.live_endpoint_smoke --live
./scripts/package_macos.sh
git diff --check
```

Use a compiled host radio decoder and set FIB_RADIO_FIXTURE to a host-only MP3
fixture for the optional decoder fixture tests. The fixture is never played on
the Flipper. C sanitizer commands are in .github/workflows/build.yml; macOS needs
a compiler and sysroot from the same toolchain.

Catalog submission separately uses the current official tools/bundle.py on the
full immutable source SHA after pushing it. Validator output and the submitted
SHA are recorded in the update PR, not invented before the source commit exists.
