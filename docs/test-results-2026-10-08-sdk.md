# Internet Bridge SDK follow-up — 2026-10-08

## Delivered

- Backward-compatible USB `start()` plus explicit USB/Bluetooth/None connection
  selection and disconnect. Selected-channel status fields clarify the legacy
  `usb_connected` alias without breaking its existing meaning.
- A small locked pairing snapshot, nonce-bound computer approval, code display,
  public-ID enumeration and recognition revocation. No credential tokens escape.
  Recognition storage is shared with the main FAP; desktop internet consent is
  still separate. No protocol or native desktop changes were needed.
- Optional `fib_bridge_client_connect_ui()` handles the modal chooser, peer
  approval, code screen and consent wait. Back cleans up; active requests are not
  interrupted. The caller resumes its event loop and streams GET chunks normally.
- Dependency-free SDK export, including both missing pairing sources/headers,
  an MIT license and no Toolbox/radio sources. It refuses existing destinations
  and symlinks. Consumer builds use two wildcard source patterns.
- Standalone example actually builds the exported tree with those patterns;
  main source manifest/FAP is not replaced by the example build. Updated quick
  start, lifecycle rules, compatibility notes and CI test commands.

## Checks

- Main FAP and exported standalone example: uFBT Target 7/API 87.1 import
  validation passed. No invented SDK calls; GUI, input, queue and mutex APIs were
  checked in installed official headers and used in the real FAP build.
- SDK adapter and modal UI: ASan/UBSan with strict C warnings passed. Session/GUI
  doubles cover USB, new/known BLE peer, stale displayed nonce, denial, Back
  during code, retry after permission denial, start failure, active-request
  protection, callback forwarding, public-ID bounds and complete resource cleanup.
  These doubles do not emulate BLE RF or real firmware scheduling.
- Python: 91 passed, including complete export, filenames with spaces, refusal
  of existing/dangling-symlink destinations and missing-source failure. The full
  host test run used the existing host-only MP3 fixture, not a Flipper tone.
- Swift: 111 executed, 105 passed, six opt-in live/hardware tests skipped;
  warnings treated as errors, zero failures. No Swift files changed in this task.
- Shell syntax, both workflow YAML files and `git diff --check` passed locally.
  Windows CI gains the export test without importing POSIX-only PTY tests. A
  host lacking symlink privileges skips that filesystem subtest.

## Physical Flipper evidence and remaining boundary

The temporary `Bridge SDK Client` FAP ran on the attached Flipper:

- Actual chooser displayed USB/Bluetooth. USB selection re-enumerated dual CDC
  normally; live HELLO reached the native Mac's internet-permission gate.
- Permission was not bypassed. The Mac was locked, so the requested Allow Once
  approval and the sample HTTPS GET could not be completed in this test run.
- Bluetooth mode started and displayed `Enable Bluetooth on computer`. No
  connected BLE peer/code/consent or BLE HTTPS completion is claimed.
- Back during USB setup and during Bluetooth setup exited cleanly; `loader info`
  reported no application running afterwards, with no reset required.
- The temporary example was removed from the device after testing (its local
  staged FAP remains recoverable). The previous main FAP was saved under
  `.build-tests/fap-before-sdk-api.76Fq8D/` before installing the rebuilt main
  application in Tools, with local/device MD5
  `31d5338f02403c794b90b7f7f82558ef` matching. Its normal USB/Bluetooth selector
  was confirmed on the physical display (`sdk-main-restored.png`).

Actual pixel captures under `.build-tests/`: `sdk-connection-chooser.png`,
`sdk-usb-connected.png`, `sdk-bluetooth-setup-final.png`.

Still required for end-to-end SDK hardware certification: unlock the desktop,
grant USB consent and get a sample response; enable Bluetooth in the companion,
select the displayed computer, enter a new-peer code, grant separate consent,
perform a GET, and test revoke/re-pair on real peers. Native Windows/Linux BLE
and their keyring/serial paths still require those target platforms.

No persistent permission, pairing token, system Bluetooth bond or OS privacy
setting was changed by testing. No Git commit, push, release or Catalog update.

## Reproduce

```sh
UFBT=/path/to/ufbt ./scripts/build_sdk_example.sh
python3 -m unittest tests.test_sdk_export -q
clang -std=c11 -Wall -Wextra -Werror -pedantic -Itests/sdk_stubs -I. \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  sdk/flipper/fib_bridge_client.c sdk/flipper/fib_bridge_setup.c \
  tests/test_bridge_sdk.c -o .build-tests/test_bridge_sdk
./.build-tests/test_bridge_sdk
swift test --package-path macos -Xswiftc -warnings-as-errors
```

On this Mac, sanitizer tests used the matched Command Line Tools clang and
MacOSX27.0 SDK. The exporter works with Python's standard library; the complete
consumer remains in the temporary directory printed by the build script.
