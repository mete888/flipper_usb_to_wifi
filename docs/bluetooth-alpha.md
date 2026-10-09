# Historical Bluetooth development notes

The product is now **Internet Bridge**, with separate USB and Bluetooth entry
points. The Demo/Alpha navigation and single-helper switching below are
historical. See [current connection modes](connection-modes.md). Bluetooth radio
is disabled; USB uses the bounded host-decoded PCM pipeline.

## Bluetooth Alpha (historical, unreleased)

The experiment was originally labelled Beta. It is now called **Alpha** because
physical pairing and wireless HTTPS validation are not complete. Earlier screen
and log checks below used the former Beta label; this rename does not change the
GATT UUIDs, framing, security model, or default USB behavior.

This opt-in experiment replaces the active USB bridge channel with BLE. USB is
still the default on every app launch. The native macOS helper and opt-in
Windows/Linux terminal hosts have BLE backends. The public client SDK remains
USB-first. Windows/Linux native Bluetooth has not been physically certified.

## Try it

1. Build/install this FAP and the matching v0.5.0 macOS app.
2. On Flipper, open the **last main-menu item: Demo**. Change **Bluetooth Alpha**
   from **Off** to **On** with Left/Right.
3. In the Mac helper's menu, open **Demo… → Connection**, then enable
   **Bluetooth Alpha**. macOS may request
   Bluetooth access. The helper requires the firmware serial discovery marker
   plus the **FZ Bridge** advertised local name before attempting a connection.
4. For the first native BLE link, macOS may ask for its system PIN. In Flipper
   **Demo → Requests** (Connection Requests), select `Bridge PC - AB12` (the suffix is a
   random installation ID), then **Pair** or **Connect**. Alpha On alone no
   longer displays a bridge code. Reject/Back on the request detail dismisses
   that connection while leaving Alpha On. The helper waits for selection.
   New peers show a **bridge pairing code** only after Pair: enter it into the
   helper. Recognized peers use Connect without a code. Neither grants internet
   permission; the Mac still asks Allow Once / Deny.
5. Select **Allow Once** in the helper's internet permission prompt.
6. Test **Test Connection**, then **Get Sample Text**. Disconnect the USB cable
   to establish that successful requests really use BLE (not a charging cable).
7. To resume USB, turn Alpha off on **both** devices and reconnect the cable.

### Internet Radio is USB-only

The Bluetooth radio experiment was removed on 2026-10-07 at the user's request.
Bits 7–9 are no longer advertised or accepted. Bluetooth responses negotiate
at most 8 KiB; former radio headers are rejected before network execution.
No PCM/ADPCM converter or FFmpeg process remains in the helper. Pairings and
Bluetooth text tools are unchanged. Turn Demo Off on both peers to use USB.
The USB MP3 decoder, speaker rate, DSP and reservoirs match the original path.
The enlarged full FAP still needs physical RAM validation for USB playback;
restoring source is not proof of audible playback.

## Mac Bluetooth management and revocation

The menu's **Demo…** button opens a dedicated window with
**Connection** and **Pairings** tabs. The main **Revoke Device Permission**
button remains internet-only. It does not remove pairing records or OS bonds.
USB behavior and its Allow Once / Always Allow / Deny prompt are unchanged.

Pairings lists up to 64 Flippers that completed this bridge's code exchange on
authenticated, encrypted GATT. It is **not** the macOS system bond list. Public
display metadata and a random host identifier are stored in local preferences;
256-bit bridge recognition tokens are stored in **Keychain**. The matching
Flipper credential is stored only under this application's own microSD directory.
Legacy v1 observed-device/blocked records are not trusted or displayed as v2
pairings: complete the bridge code once after upgrading both components.

- **Revoke Pairing…** asks for confirmation, deletes the selected credential
  and row, and ends its active Bluetooth session. There is no Block, Pair Again,
  or Open Bluetooth Settings button. The next connection requires a new bridge
  code. Other Flippers and stored USB grants are unchanged.
- If Flipper is disconnected, its old credential cannot be removed immediately.
  The Mac no longer presents it, so the next link still requires code; successful
  re-pairing replaces the old credential for this host. Active revocation also
  attempts to delete the matching credential on Flipper before disconnecting.
- A revoked row does not return merely because a HELLO/status arrives late. It
  returns only after a fresh successful bridge code exchange.
- Every new BLE FIBP connection still gets **Allow Once / Deny**, even for a
  recognized peer. Normal internet revocation does not force re-pairing.
- No Flipper-wide “forget all devices” operation or firmware pairing UI change
  was added; doing so would also affect unrelated phone pairings.

This does **not** reset/force macOS's native Bluetooth PIN or delete system
bonds. Native BLE encryption/authentication stays enabled. Re-pairing after an
app revoke uses the bridge code instead, without asking the user to find an
ambiguous "Bluetooth Device" entry in system settings. Removing firmware-wide
bonds would affect unrelated phones, so the FAP never does that.

### Flipper Pairings

Open **Demo → Pairings** for **Known Computers**, with eight records per page.
Names such as `PC 78233E7B` are random host-ID aliases, not computer names.
Select a record, then **Keep / Revoke**. Revoke removes its app recognition
credential and ends that host's active BLE session, if connected; other hosts
and USB permissions are unchanged. An active host receives a nonce/host-bound
revocation notice so its row can disappear too. If disconnected, its stale row
is removed when the next connection requests a new bridge code. There is no
instant remote deletion while offline. Listing and confirmation have been
checked on a real Flipper; active two-sided revocation still needs a hardware
regression test.

### Windows/Linux Demo

Install the optional Bluetooth dependencies and run `fib-bridge --demo`.
The existing hosts are terminal applications, not native GUI apps: their Demo
menu offers Enable Bluetooth Alpha, Pairings and Revoke Pairing. BLE tokens
require a secure OS keyring; plaintext/null backends fail closed. Native system
pairing, manual Flipper Pair/Connect and fresh Allow Once/Deny remain separate.
See [host setup, commands and limitations](../host/README.md#demo-bluetooth-alpha-and-pairings-opt-in).
Fake-GATT tests passed; native Windows/Linux BLE and packaged installers have
not been tested here. Existing USB operation does not require these dependencies.

Selection waits up to 120 seconds. After Pair, the bridge code lasts a separate
120 seconds and permits three submitted attempts per
connection. Cancel or expiry stops automatic prompt retries on Mac: toggle
Alpha Off/On to retry. Back on Flipper's bridge code screen cancels Alpha and
restores USB mode. See the [wire extension](../protocol/bluetooth-pairing.md).

Known Alpha transition issue: the interface can return to Off/USB but the helper
may repeatedly wait for HELLO after cancelling the code screen. Close/reopen
the FAP and helper to recover; this recovered HELLO during the latest hardware
check. Automatic return to a working USB session is not yet a clean pass.

### Bridge pairing redesign — 2026-10-05

This supersedes the block/manual-Forget UI described in the historical test
record below. Both the FAP and helper must be updated together. Native BLE,
two-device recognition/revocation and cable-unplugged HTTPS remain hardware
tests to run: the Flipper was disconnected during this change.
See the [current software validation record](test-results-2026-10-05.md).

### Historical Pairings UI validation — 2026-10-04 (superseded)

The Swift suite executed 87 tests: 85 passed and two optional live-provider
checks were skipped in offline mode, with zero failures. Nine new pairing-store
tests cover peer-specific revocation, reconnect blocking, persistence, explicit
re-enable, internet/pairing separation, bounded records, sanitized names and
concurrent updates. The release build also passed with warnings treated as
errors. The existing 45 Python tests passed.

Native UI validation was attempted but computer control reported that the Mac
was locked and could not be unlocked automatically. Tab layout, system-settings
navigation, modal confirmations, real device disconnect and Forget/re-pair PIN
behavior therefore remain unverified on hardware. No pairing was revoked,
no OS bond was deleted and no permission was auto-approved during these tests.
The Flipper FAP and firmware pairing behavior were not changed for this UI work.

## Transport contract

- A separate `FuriHalBleProfileTemplate` is necessary: the built-in serial profile
  belongs to firmware RPC and reclaims its callbacks on connection. The custom
  template prevents RPC from opening on our channel.
- Discovery: existing firmware serial 16-bit marker `0x3080` + advertised local
  name prefix `FZ Bridge `. This is not an assigned new/private 16-bit UUID and
  is not authentication. Ordinary Flipper/mobile names are ignored. The previous
  private 128-bit discovery marker exceeded the legacy advertising budget when
  combined with the bridge name, preventing discovery despite Alpha showing On.
  With flags, TX power, maximum 16-byte name and 16-bit marker the budget is
  28/31 bytes; build assertion and Swift/C tests cover it. GATT and code gates
  remain mandatory after discovery. Both FAP/helper must be updated for this fix.
- GATT service: `8FE5B3D5-2E7F-4A98-2A48-7ACC60FE0000`.
- TX/RX/flow/status use the official serial service UUIDs (BLE little-endian
  constants converted to CoreBluetooth UUID notation).
- Subscribe to TX indications and flow notifications; perform an authenticated
  flow-control read. Then complete bridge recognition before handing the
  buffered HELLO to the internet coordinator. No network consent or request
  can bypass this additional BLE-only gate.
- Write four zero bytes to the status characteristic to open this **private**
  profile's FIBP channel. Its reset-request callback means channel-ready here,
  never "restart firmware RPC". No text/binary mixture or data-channel preamble.
- The data characteristics carry the existing FIBP v1 byte stream unchanged,
  including both CRCs, request IDs, sequence checks and bounded parsing. GATT
  transfer boundaries are not FIBP frame boundaries.
- Flipper TX uses acknowledged 20-byte indications (safe minimum ATT MTU).
  Host TX uses acknowledged writes up to 128 bytes or the negotiated MTU limit.
- Host respects the peripheral's big-endian receive credits (2,048 bytes) and
  waits for renewal. One encoded frame is active; at most one additional bounded
  auth frame can wait for a GATT ACK/indication ordering race. Four-second
  write deadlines and disconnects release the waiter; no unbounded TX backlog.
- Partial/uncertain indication delivery closes the channel rather than silently
  reusing it. Loss of the BLE connection clears permission and cancels HTTPS.

## Alpha limits and security

- HTTPS GET only, one active request, 8 KiB text response cap, existing 1,536-byte
  display preview. Radio is a separate negotiated 4 MiB bounded stream.
- Bluetooth authenticated/encrypted GATT and OS PIN pairing are retained.
- Bridge tokens are bearer credentials, not device attestation. They are sent
  only over authenticated/encrypted GATT; frame CRC is not authentication. A
  compromised Mac, readable Flipper microSD credential, or physical control of
  the displayed code compromises this recognition layer. Flipper storage has
  no hardware-protected Keychain equivalent. Codes/tokens are never logged.
- Every BLE connection requires fresh internet consent. USB persistent grants
  do not apply to Bluetooth; no Always Allow option is shown for this alpha.
- A device name, advertisement UUID, and self-reported hardware UID are **not**
  cryptographic proof of a genuine Flipper. Check the physical pairing PIN.
- Existing HTTPS/SSRF, ephemeral-session, cookie/credential and redirect policies
  are unchanged, including the documented URLSession DNS-rebinding limitation.
- BLE link loss is detected by the Bluetooth stack's supervision timeout, not
  instantaneously when a device leaves radio range. Requests retain timeouts.
- Bluetooth performance, radio support and cross-platform BLE are not promised.
- The enlarged local Toolbox/Alpha build has tight RAM headroom on some firmware.
  Radio buffers are lazy and playback is rejected with a memory-busy message
  when its unchanged buffers/decoder stack cannot fit. Do not assume radio was
  hardware-regression-tested just because the FAP builds.

## Validation

See the [subsequent full QA run](test-results-2026-10-04.md) for the live-provider
failure, failed Alpha enable/recovery attempt and current hardware boundaries.
The earlier local record below does not certify wireless operation.

```sh
ufbt
cd macos && swift test
# From the repository root:
./scripts/package_macos.sh
```

Unit tests cover fragmented FIBP packets, default USB identity, disabled-BLE
writes, fresh-per-connection consent, response bounds, and disconnect cancellation.
These do not replace physical pairing or a cable-unplugged HTTPS test.

For an operator test when the menu bar cannot be controlled remotely, start the
packaged helper with explicit, non-persistent options:

```sh
"dist/macos/Flipper Internet Bridge.app/Contents/MacOS/FlipperInternetBridge" \
  --bluetooth-alpha --diagnostics-stderr
```

Quit the existing helper first so that only one instance owns the device. These
options select the same Alpha backend and mirror the existing sanitized
diagnostics to stderr; they do **not** approve Bluetooth access, PIN pairing, or
internet permission. A normal launch still defaults to USB.

### Local check record — 2026-10-04

- Standard uFBT Target 7 / API 87.1 main FAP and standalone SDK example: build
  and import validation passed. Main demo FAP installed and launched on Mico.
- Swift: 78 tests, zero failures; two optional live-provider tests skipped.
- Python: 45 tests passed. Toolbox UI host-double ASan/UBSan check passed.
- Native macOS arm64 app/DMG rebuilt; ad-hoc signature and Info.plist validated.
- Real Flipper framebuffer checks: Demo defaults Off; On switches Connection
  Info to BLE Beta; returning to USB restores USB/host discovery. The final
  fresh USB session showed Connected / Found / Pending permission.
- With the final heap preflights, enabling Beta without an active RPC screen
  stream succeeded and the real display showed On. An operator screen stream
  consumes extra RAM and can make the guarded profile start refuse/revert Off;
  close qFlipper/screen sharing before toggling on a low-memory firmware.
- First hardware attempt exposed an OOM at startup. Lazy radio reservoirs
  fixed the subsequent launch; explicit heap preflights now reject playback or
  profile start when there is insufficient headroom. USB audio playback has
  NOT been re-certified on this enlarged alpha build.
- BLE OS permission, PIN pairing, HELLO/PING/PONG and cable-unplugged HTTPS:
  **pending user pairing / internet consent**, not claimed as hardware-passed.
- Follow-up remote test: the actual Flipper screen still showed Beta On. The
  packaged helper launched with the former `--bluetooth-beta` switch (now
  `--bluetooth-alpha`) and `--diagnostics-stderr`, stopped
  its USB coordinator, and started CoreBluetooth. Native automation could not
  bind the windowless menu app; access to macOS UserNotificationCenter was
  explicitly refused by the computer-control tool. No system approval or
  internet consent was bypassed. An operator must complete any pending system
  Bluetooth permission before pairing/HELLO/GET can be tested. Re-run Swift
  tests: 78 executed, two optional live-provider checks skipped, zero failures;
  Python: 45 passed. This is still **not a passed wireless network test**.
- Alpha rename: the main FAP was rebuilt, passed API 87.1 import validation,
  installed and launched on Mico; the packaged macOS app/DMG was rebuilt and its
  ad-hoc signature verified. Swift: 78 tests, two skipped, zero failures. USB is
  still the default. A fresh native-UI attempt explicitly reported that the Mac
  was locked and automatic unlock failed. The Mac must be unlocked manually;
  no lock-screen, Bluetooth permission, or internet-consent bypass was attempted.
- No source push or Catalog update was performed for this experiment.

Manual checks before publishing: allow/deny macOS Bluetooth permission, accept/
reject pairing, allow/deny internet access, PING/PONG, short GET without USB,
disconnect during a request, toggle back to USB, repeat toggles, exit the FAP,
verify normal firmware BLE and existing USB tools still work. Keep this alpha out
of a Catalog submission until those checks are recorded.

API references: [Flipper profile interface](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/targets/f7/ble_glue/furi_ble/profile_interface.h),
[serial service](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/targets/f7/ble_glue/services/serial_service.c),
[CoreBluetooth](https://developer.apple.com/documentation/corebluetooth).
