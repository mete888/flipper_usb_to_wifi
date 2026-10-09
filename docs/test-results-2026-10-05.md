# Bluetooth bridge recognition QA — 2026-10-05

## Latest follow-up: early playback stops and station parity

Local fixes only; no publication. New FAP SHA256:
`6ebe2675bdadb2f2b947a75f1748d09a3f949d3e9e45ce9cb25fb89fe18603db`.
Exact dist/device MD5 matched during SDK upload; loader open was requested.

- Found reproducible native converter failures: full decoded buffers were
  treated as fatal, and coalesced URLSession deliveries >64 KiB were rejected.
  Decoded output now blocks its pipe reader at 128 KiB; asynchronous pipe
  writes suspend/resume only their URLSession task. Input stays within the
  existing 4 MiB whole-request cap. UI/network delegate queues never wait.
- A generated 180 s / 64 kbps MP3 fixture reproduced cancellation killing the
  test process with SIGPIPE while its writer was blocked. Per-descriptor
  F_SETNOSIGPIPE was verified in Apple's installed SDK header and added.
  The same test now passes, including 128 KiB deliveries, a full reservoir,
  valid ADPCM output and cancellation waking the blocked writer within 3 s.
  It generates in-memory media only, not a user asset or physical speaker test.
- Actual HTTPS 64 kbps WALM source passed with a simulated 2 s delay on every
  output callback: >=16 KiB complete ADPCM blocks and cancellation (37.3 s).
  The previous NOAA native attempt failed before audio with invalidResponse;
  its curl GET returned HTTP 200 later. This provider-specific result remains
  unresolved; no TLS/SSRF or HTTP acceptance rules were weakened to hide it.
- Both discovery pipelines now use the same 8–64 kbps HTTPS MP3 query/filter
  as USB; Flipper's BLE-only station filter was removed and its USB fallback
  enabled on BLE. Legacy direct-MP3 rate validation still caps at 32 kbps.
  Python actual mocked request/transform pipeline returns identical USB/BLE
  station sets. Live Radio Browser US query returned five entries, including
  three 64 kbps sources. This is not a promise of all codecs/bitrates/stations.
- A stopped decoder now cancels its active request, switches to a stopped UI
  and exposes speaker/timer or invalid-audio failure instead of silently
  appearing to play or leaving a retry loop armed.
- Swift: 105 executed, 101 passed, four opt-in checks skipped, zero failures;
  FFmpeg stress and slow live tests ran separately and passed as above.
  Python: 68/68 passed. FAP build/import and Mac App/DMG signature passed.

Real BLE playback/gap counts and Stop/Back still require the user's test.
At inspection the device was back on its home screen and the Mac helper was
not running, so the original on-device error was not recoverable. Asked for
the station name, displayed error and time-to-stop; those details are pending.
Do not equate the simulated slow sink with a physical Bluetooth regression pass.

## Latest follow-up: Bluetooth radio continuity / ADPCM

Local Alpha only; no commit, GitHub/Catalog push or firmware replacement.

- FAP build/import passed, Target 7 / API 87.1. Exact dist was uploaded with
  the official SDK storage tool; device/local MD5 matched before loader open.
  Final FAP SHA256: `7589536cf7d9e160a9484137642a0da0224e34c64921aabc20ea166484488293`.
  `ufbt launch` first returned Errno 6 during USB re-enumeration; the subsequent
  split exact-file upload/open recovered. The main menu and Demo Off screen
  were captured on the physical device; this is not a BLE audio pass.
- Swift full suite: 104 executed, 101 passed, three opt-in live checks skipped,
  zero failures. Release warnings-as-errors build and App/DMG/signature passed.
- Python: 67/67 passed. New shared C/Swift/Python ramp vectors, silence,
  malformed block/index/reserved/padding, fragmented delivery, response renewal,
  resampling pitch, cancellation and missed-tick media-clock checks passed.
  Portable C PCM/ADPCM ASan/UBSan passed.
- Native live HTTPS + FFmpeg + ADPCM received at least 64 KiB, checked the
  format-2 header and complete blocks, and cancelled cleanly (36.4 s test).
  Terminal-host live pipeline returned HTTP 200, 16,908 bytes / 128 complete
  ADPCM blocks and cancelled. Used the existing trusted CA bundle as before;
  TLS/SSRF/permissions were not bypassed. No converter child remained.

The change halves audio bandwidth: 256 samples / 132 bytes at 4 kHz (~2063 B/s).
Flipper now has a 6 KiB compressed reservoir (~3 s), a 2 KiB-stack audio worker,
and an 8 KiB final PCM ring. A 4 KiB compressed prebuffer and absolute host
sample clock compensate for short stalls and lost scheduling ticks. USB still
uses its original 16 KiB ring, thresholds and DSP. BLE startup keeps a 24 KiB
free-heap / 9 KiB contiguous-block guard. Physical CLI reported 28,952 bytes
free before this final small-ring build; RPC capture itself consumes memory.

Native Mac UI automation failed to initialize. The existing operator
`--bluetooth-alpha` option started the real helper with CoreBluetooth authorized
and scanning, without granting pairing/internet access. Remote Flipper toggle
attempts left Alpha Off; user input was requested. Actual speaker continuity,
startup heap/stack margins, Stop/Back/repeated Play under BLE and minimum-MTU
behavior remain unverified until the user completes the wireless radio test.
Windows/Linux source/tests are updated; native hardware/binaries are not tested.
The software/live HTTPS passes do not establish gap-free Bluetooth playback.

## Latest follow-up: experimental Bluetooth radio

Local Alpha, not published. This section supersedes earlier counts/hashes.

- Main FAP and standalone SDK example build/import validation passed (Target 7,
  API 87.1). Final source FAP SHA256:
  `5f3d71563c61dca439f03effa939b4e379fc8985eb16782537a15d130c00208f`.
  Upload completed and actual device/dist MD5 matched. `ufbt launch` hit
  Errno 6 during dual-CDC re-enumeration; split SDK upload/loader commands
  recovered without replacing firmware or deleting pairings.
- Swift: 100 executed, 97 passed, 3 opt-in live checks skipped, no failures.
  Separate opt-in live radio check passed with the real HTTPSNetworkClient:
  >16 KiB received from `https://wxradio.org/TX-Dallas-KEC56` in a direct
  32 kbps profile, followed by a 64 KiB actual FFmpeg → 4 kHz mono PCM check,
  including two buffer compactions, exact 12-byte header validation and
  cancellation. The longer test passed after fixing sliced Data indexing in
  the native converter. These are computer-side network
  tests, not BLE or speaker passes.
- Python: 64/64 passed. Separate live PCM pipeline returned HTTP 200,
  validated its header, emitted 16,524 bytes and cancelled. The SDK toolchain's
  default OpenSSL CA path did not exist; the rerun used the existing macOS
  `/etc/ssl/cert.pem` via SSL_CERT_FILE. TLS verification was never disabled.
- New portable PCM C ASan/UBSan passed: fragmented header, exact resampled
  sample count/pitch, invalid format, and cancelled output. Added it to CI.
- Native App/DMG rebuilt and ad-hoc signature checked; Windows/Linux packaging
  CI includes the new tests but was not dispatched. No orphan FFmpeg process
  remained after the live conversion/cancel checks.

The actual device's CLI reported 38,256 bytes free with the previous expanded
FAP active, versus the MP3 player's 76 KiB startup guard. This motivated the
new PCM mode: no decoder heap/24 KiB stack/compressed reservoir; an existing
16 KiB audio ring, a small parser, and a 24 KiB guarded startup budget instead.
USB's MP3/DSP path is retained, not recertified: its heap guard may still reject
playback in a memory-constrained full build. No USB sound-quality claim is made.

Real BLE speaker sound, gap counts, Stop/Back, repeated Play, transport switching
and radio pairing revocation still require the live Flipper/user permission
test. No code was entered and no new internet grant or pairing was made by the
software tests. Windows/Linux actual hardware is unverified. FFmpeg is already
installed on this Mac, optional elsewhere and not bundled/downloaded by the app.
See [radio wire/security/limit contract](../protocol/bluetooth-radio.md).

Radio checklist: rebuild both peers; Alpha On; manually Pair/Connect; user code
entry when required; fresh Allow Once. Search US in Toolbox > Internet Radio,
select NOAA, wait for PCM buffering, then measure chunks/buffer/gaps without
USB. Test Stop, Back, Left to stations, Play again, deny, disconnect, pairing
revoke, absent FFmpeg, invalid MP3, minimum MTU and source interruption. Stop
must silence immediately and leave no child process; failure must not loop
automatic retries. Recheck USB separately.

## Earlier follow-up: Demo, Flipper Pairings and Windows/Linux BLE

Local changes only; no GitHub/Catalog publication. This section supersedes
the software counts and binary hashes in earlier records below.

- Main FAP build/import passed, Target 7 / API 87.1. `ufbt launch` uploaded
  and opened the updated app. Device/dist MD5 matched. Dist SHA256:
  `70013edf2009e04d720e26b531e311ff55c6ae085e50c0676eeee5ccd3dfa687`.
  The standalone SDK example was rebuilt and passed import validation too.
- Swift full suite: 94 executed, 92 passed, two opt-in live-provider skips,
  zero failures. Release warnings-as-errors build and arm64 App/DMG packaging
  and ad-hoc signature verification passed.
- Python full suite in an isolated environment with pyserial: 58 executed,
  58 passed, zero skips/failures. This includes 13 new Bluetooth tests and the
  pseudo-terminal USB integration test. No actual OS keyring was changed.
- BLE pairing C ASan/UBSan passed, including exact credential-filename parsing.
  Other portable C suites' earlier passes are recorded below, not new results.
- Installed the optional host Bluetooth extra in an ignored test environment;
  Bleak 3.0.2 and keyring 25.7.0 API surfaces and CLI help were checked.
  Fake GATT/flow-control and in-memory vault tests are not native BLE validation.
- Git whitespace checks passed. Windows/Linux packaging CI was updated to
  collect Bleak/keyring; it was not dispatched or published.
- The macOS source and rebuilt package use Demo…/Demo for the menu/window.
  Native accessibility interaction timed out while a permission dialog was
  open, so the renamed menu was not visually certified on this run.

On the actual Flipper, Demo > Pairings displayed Known Computers and the user's
existing host-ID alias. Its Keep/Revoke confirmation was captured from the
framebuffer, then cancelled with Back. The user's pairing was deliberately
preserved: physical deletion, active two-sided revocation, and a subsequent
new-code exchange were not tested. macOS now serializes pairing revocation;
this addresses the earlier write-order race in software, not a certified
hardware regression pass. Offline revocation invalidates recognition locally;
the disconnected peer cannot immediately remove its row and cleans up when
it next receives a code-required response.

Windows/Linux remain terminal hosts, now with `fib-bridge --demo`, `--pairings`
and `--revoke-pairing`. There are no newly claimed native GUI applications.
Real WinRT/BlueZ connections, keyring integration, packaged binaries, and
cable-unplugged HTTPS still require platform/device tests. The earlier
Alpha-to-USB cancellation recovery limitation remains open.

## Earlier follow-up: manual Flipper request selection

Alpha On now listens without generating a bridge code. Demo > Requests opens
Connection Requests with one incoming candidate (`Mac Bridge - <host suffix>`).
Pair/Connect requires physical selection; Reject and request-detail Back deny
the candidate. No computer hostname/username is disclosed. USB is unchanged.
Native OS PIN timing is separate from this application-level selection.

Current software validation:

- Main FAP build/import: Target 7, API 87.1 passed; final `ufbt launch` completed
  upload and launch. Device MD5 equals the final SDK/dist binary. Dist SHA256:
  `69462ed3c82369c7eaf5539f28d3e000379623bb768ce8bc67069957900fd9a2`
  (relinked by the final recovery launch; source unchanged).
- Swift full suite: 93 executed, 91 passed, 2 optional live-provider checks
  skipped, zero failures. Final Bluetooth-focused rerun: 21 passed. Release
  warnings-as-errors build and arm64 App/DMG packaging/signature checks passed.
- Python: 45 executed, 44 passed, 1 optional dependency skip, zero failures.
- Six C groups passed ASan/UBSan: protocol, markets, Toolbox tools/cards/UI host
  doubles and BLE pairing. SDK example build/import also passed (before the
  final transport readvertising and short menu-label adjustments).
- Tests cover no code/acceptance before approval (new and known hosts), premature
  CODE, legacy capability rejection, repeated WAITING, Reject, independent
  selection/code lifetimes, wrong-code retries not renewing expiry, and tick wrap.

Real Flipper + Mac evidence, USB cable retained for screen-capture RPC only:

1. Alpha On remained on Demo with one pending request, **no bridge-code screen**.
   Mac logs stopped at `waiting for Pair/Connect on Flipper`; the internet
   coordinator was not given a Bluetooth device/HELLO yet.
2. Request list and new-computer Pair/Reject screen were captured from the
   actual framebuffer. Back removed the candidate and Mac logged rejection.
3. Reject button also removed the candidate while leaving Alpha On. This
   exposed that firmware `bt_disconnect` stops advertising as well as the link;
   the owned Alpha profile now explicitly resumes advertising after that call.
   A new helper session rediscovered and reached WAITING **without toggling
   Flipper Alpha Off/On**, verifying this fix on hardware.
4. Selecting Pair opened Flipper's six-digit code screen; Mac then logged code
   required. No code was entered or credential persisted during this test.
   The test was cancelled with Flipper Back to restore USB.

Boundaries: recognized-peer Connect, code entry → fresh internet consent, and
cable-unplugged HTTPS were not repeated physically for this revision. These are
not claimed as hardware passes. The earlier active-pairing revocation write race
below remains a separate open issue. AppKit accessibility inspection timed out;
Mac modal layout was not newly certified. No GitHub/Catalog publication occurred.
After cancelling the code with Back, Flipper showed Alpha Off and the USB ports
returned, but a fresh helper repeatedly waited for USB HELLO. This is not a
clean USB transition pass; the existing transport was not changed by this task.
Reopening the FAP with `ufbt launch` and restarting the USB helper recovered:
native logs then confirmed `FIBP HELLO validated: Mico`. Device/dist MD5 was
rechecked after that launch. No permission was granted and no HTTPS request
was made in this recovery check.

Firmware reference for the observed advertising behavior:
[bt_close_connection / bt_disconnect](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/applications/services/bt/bt_service/bt.c#L403),
[synchronous HAL advertising stop](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/targets/f7/furi_hal/furi_hal_bt.c#L244).

## Earlier redesign record (before manual selection)

Local, unreleased Alpha changes. No GitHub push/Catalog update. The user's
Flipper was disconnected during the initial redesign. The first FAP was later
uploaded with `ufbt launch`, its device MD5 matched the uploaded build, and its
real main-menu framebuffer was captured. No wireless success is claimed.

## Follow-up: discovery failure

The user enabled Alpha on both devices but no code appeared. Mac diagnostics
showed Bluetooth authorized (3), adapter powered on (5), and scanning, but no
candidate reached GATT. Inspecting the SDK's actual GAP implementation exposed
an advertising-budget defect: the previous 128-bit marker plus long bridge name
exceeded legacy 31-byte advertising, although profile start could still return
success and the UI stay On. Discovery now uses the existing firmware serial
16-bit marker `0x3080` + strict `FZ Bridge ` advertised-name filter (not cached OS
name). Budget = 28/31 including flags, TX power and maximum name. Authenticated
GATT, bridge recognition and separate internet consent are unchanged.

Flipper was initially no longer visible over USB during diagnosis. After the
user switched Alpha Off and reconnected, the corrected FAP was uploaded, its
device MD5 matched the uploaded binary, and its real main-menu frame was captured.
The user then enabled Alpha: native logs confirmed advertisement discovery, BLE
connection, GATT service discovery, HELLO, and bridge-code-required. Recognition
was subsequently accepted and the Mac displayed Connected (Bluetooth Alpha).
The user also reported the code flow working. Internet GET and a cable-unplugged
request have not been certified. During pairing revocation the log contained a
serial-write failure before disconnect/reconnect/code-required; this possible
revocation ordering race remains open, not recorded as a clean hardware pass.
The user switched back to USB and closed the helper after testing.
The
new discovery-budget/name-filter test and 20 Bluetooth-related Swift tests passed,
as did the C sanitizer group. The full rerun/package results below refer to the
current software, not proof of wireless behavior.

## Software results

- Main FAP: standard uFBT Target 7 / API 87.1 build/import validation passed.
- Standalone SDK example: build/import validation passed with the new BLE
  recognition modules included in its explicit source/staging lists.
- Swift: 92 tests executed, 90 passed, 2 opt-in live-provider checks skipped,
  zero failures. Release build passed with `-warnings-as-errors`.
- Python: 45 tests executed, 44 passed, 1 optional dependency check skipped,
  zero failures. Cross-platform hosts' USB behavior was not changed here.
- Six portable C groups passed ASan/UBSan: protocol, markets, Toolbox tools,
  Toolbox cards, Toolbox UI host doubles, and new BLE recognition state machine.
- arm64 macOS app and DMG rebuilt; local ad-hoc signature verified.
- `git diff --check` passed. Native AppKit UI control timed out, so modal layout,
  keyboard focus, cached OS name, and interactive actions are not certified.

## New automated coverage

Recognition covers new-code flow, cached-token resume, revoked Mac credential,
lost Flipper credential, identity changes, late HELLO after deletion, unrelated
device preservation, internet/USB permission separation, persistence, sanitization,
64-record Mac limit, concurrent snapshot/rename/revoke, and failing vault writes
or deletes. Tests inject an in-memory vault: they do not change actual Keychain,
OS Bluetooth bonds, or user internet grants.

Wire tests cover exact 56/62/9/24-byte payloads, nonce echo, version/capability
fail-closed behavior, wrong length/flags/order, invalid codes, three-attempt limit,
120-second expiry, tick wrap, storage failure, zero-token rejection, and ordinary
CRC framing across 20-byte fragments. The native transport also retains at most
one next pairing frame for the indication-before-write-ACK ordering case; this
CoreBluetooth callback order has not been reproduced on hardware.

## Physical checklist (requires the user/device)

1. Install **both** this FAP and Mac helper. Close qFlipper screen streaming to
   preserve heap. Do not confuse native BLE PIN with the additional bridge code.
2. Disconnect USB. Enable Flipper Demo > Bluetooth Alpha and Mac Connection >
   Enable Bluetooth Alpha. Discovery must work without USB. The advertisement
   should be `FZ Bridge Mico` (six-character suffix limit for longer names).
3. New device: native BLE may ask its PIN. Select Mac in Demo > Requests, then
   Pair: only now should the bridge code appear on Flipper. Enter
   it on Mac. Only then should Allow Once/Deny appear. Deny must perform no GET.
4. Allow Once: Test Connection and Get Sample Text must work without a cable.
5. Disable/re-enable Alpha: select the recognized Mac in Demo > Requests and
   Connect. The peer skips bridge code but asks internet
   permission again. Native OS PIN may be cached; that is not app recognition.
6. Mac Pairings > Revoke Pairing: row disappears entirely. Reconnect requires
   new bridge code, not Block/Pair Again/manual Bluetooth Settings. Repeat revoke
   while Flipper is disconnected, then reconnect: code must still be required.
7. Flipper Demo > Pairings > host > Revoke: local row disappears and its
   active host disconnects/removes its row; reconnect requires a new code.
   Repeat offline: desktop cleanup occurs at the next code-required handshake.
8. Normal Revoke Device Permission: internet stops, pairing stays. Other devices
   and persistent USB permissions must remain unchanged.
9. Wrong code, Cancel, Flipper Back, expiry and disconnect: no internet request,
   no stale prompt can accept a different session. After Cancel/expiry toggle
   Mac Alpha Off/On to retry. Flipper Back on code screen restores USB mode.
10. Restore USB on both sides and retest fresh USB HELLO/permission/GET. BLE is
   still Alpha: radio and post-switch heap/lifecycle need real-device regression.

## Reproduce

```sh
ufbt
UFBT=ufbt ./scripts/build_sdk_example.sh
(cd macos && swift test && swift build -c release -Xswiftc -warnings-as-errors)
python3 -m unittest discover -s tests -p 'test_*.py'
./scripts/package_macos.sh
```

Portable C compile recipes are in `.github/workflows/build.yml`. On this Mac,
CLT clang, linker and CLT macOS SDK were explicitly paired for sanitizer runs.
Software results do not replace the physical checklist above.
