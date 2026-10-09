# Internet Bridge connection-mode validation — 2026-10-07

## Scope

Display-name and lifecycle refactor, not a firmware/network-stack replacement.
The FAP appid, bundle identifier, Keychain/preferences namespaces, BLE wire
profile and SDK default USB entry point remain compatible. All Toolbox tools
are retained; Internet Radio appears only in USB mode.

## Automated/local checks

- Main FAP uFBT build/import validation: Target 7, API 87.1 passed.
- Standalone client SDK example build/import validation passed.
- Swift suite with warnings as errors: 108 executed, 102 passed, 6 optional
  fixture/live/hardware checks skipped, zero failures. New tests exercise two
  simultaneous coordinators with identical request IDs, independent consent,
  revocation/cancellation/disconnect, and owner-scoped modal queue replacement.
- Python unittest suite with the compiled decoder and host-only MP3 fixture:
  67 tests passed. No fixture was played on Flipper.
- Portable C menu, radio-player and timer checks passed with ASan/UBSan and
  warnings as errors (Command Line Tools clang/SDK matched to this Mac).
- Release macOS app/DMG created, ad-hoc signature strict verification passed;
  Info.plist lint passed. Internet Bridge.app launched successfully.
- git diff --check passed. No commit, push or Catalog change was made.

## Physical Flipper evidence

Built FAP uploaded to the existing /ext/apps/USB/usb_internet_bridge.fap path.
SD/local MD5 matched c59336d6eb29754d2768e8ed9014123c. SHA-256:
2ad05f45199427b180a703900d2ab6c27fdb6cd18015ebe7dafe4f21f8fab0dd.

Real framebuffers confirmed:

- Initial Internet Bridge selector shows USB Internet Bridge and Bluetooth
  Internet Bridge; no bridge session starts until selection.
- USB selection opens its six-entry menu and the existing radio country/station
  UI. Actual US station directory returned KLVZ, selected for the streaming check.
- Bluetooth selection opens Connection Requests / Pairings and its shared tool
  menu. Known Computers still contains the existing PC 78233E7B record; no
  credential was revoked. Toolbox retains Markets, Latest Earthquakes and
  Currency Converter; the source/menu tests also cover English Dictionary and
  radio exclusion.
- Leaving Bluetooth returns to the selector; selecting USB again opens its
  proper menu without an error or device reset.

The first USB-selection attempt used a screen RPC session, which deliberately
locks firmware USB configuration. Starting CDC was refused. Ending screen RPC
and sending the selection through the ordinary CLI succeeded; do not use an
active qFlipper/RPC screen session while changing USB mode. This refusal is a
safe SDK guard, not a reason to bypass the firmware lock.

The first radio QA run expired while the operator was navigating the remote
keyboard (zero PCM), and is NOT counted as a playback pass. The repeat ran for
83.811 seconds overall and passed the real-USB assertions: more than 30 seconds
of PCM, more than 800 KB delivered, average over 25 KB/s, exactly one stream,
zero protocol ERROR frames, and request cancellation with at most 2 KB already
accepted tail bytes after Back. The network/serial coordinator used only the
explicit operator QA in-memory Allow Once; no persistent grants were changed.

During playback the primary CLI reported free heap 32,112 bytes and largest
block 19,480 bytes. These include CLI allocations and are not peak/idle values.
No RAM refusal, Furi reset or protocol timeout occurred in the successful run.
This is real USB delivery/cancellation evidence, not a subjective listening test.

## Still requiring manual validation

Native SwiftUI panel visual review and simultaneous native USB/Bluetooth dialog
presentation were not automated because native computer control was unavailable.
Real Bluetooth discovery -> explicit Pair/Connect -> code/consent -> wireless
HTTPS, both-side revoke, and simultaneous two-device traffic must be retested
with this build before publishing. Menus/profile startup and automated tests do
not certify that complete wireless path. Windows/Linux native BLE is not tested
on this Mac. Historical Bluetooth development limits still apply.

## Repeat commands

```sh
ufbt
./scripts/build_sdk_example.sh
(cd macos && swift test -Xswiftc -warnings-as-errors)
python3 -m unittest discover -s tests -p 'test_*.py' -q
./scripts/package_macos.sh
```

Only with explicit operator consent and every normal helper closed:

```sh
cd macos
FIB_USB_RADIO_QA=1 swift test --filter RadioUSBHardwareTests
```

Choose USB -> Toolbox -> Internet Radio on the physical device, select a station,
wait for over 30 seconds of real audio delivery, then use physical Back. The QA
deadline includes manual navigation. It is never enabled in ordinary app use.
