# Bluetooth radio rollback QA — 2026-10-07

The initial results below are historical. The later **USB radio RAM fix**
section supersedes their unresolved-memory/playback status; it preserves the
full-feature app rather than installing the old baseline.

Local changes only. No commit, push, Catalog submission or firmware replacement.
The user requested removal of Bluetooth radio only, keeping Bluetooth text,
Pairings and the other Toolbox tools. They declined a separate Radio FAP and
a wider rollback of recent features.

## What changed

- Removed active Bluetooth MP3/PCM/ADPCM radio negotiation and converters from
  the FAP, native macOS helper and shared Windows/Linux terminal host.
- Removed the experimental codec/converter source and codec-only tests; replaced
  radio tests with regression tests for rejection, USB audio and cancellation.
- Bits 7–9 are reserved, not advertised. Old radio profiles fail before network
  execution. Bluetooth text responses negotiate at most 8 KiB.
- Bluetooth Radio entry explains that USB is required before opening the keyboard.
  Other Bluetooth selection, Pairings, revocation and internet consent remain.
- `radio_audio.c` and `radio_audio.h` are identical to Git HEAD `b29a8739`.
  USB retains the original on-device minimp3 decoder, 14,493 Hz speaker output,
  gain/DSP, 12 KiB compressed reservoir, 16 KiB PCM ring and 24 KiB decoder stack.
- Kept memory admission checks, bounded Stop/Back, callback-drain synchronization
  and guarded semaphore wakeups. Failed starts release the allocated player.
- Updated the README, host documentation, withdrawn protocol note and changelog.
  No FFmpeg dependency remains. Historical October 5 QA is not current behavior.

## Builds and automated tests

- uFBT main FAP and public SDK example: pass, target 7 / API 87.1.
  The SDK example initially could not locate uFBT; rerunning with the project's
  venv on PATH passed. No code or SDK API was invented to bypass that failure.
- Swift offline suite: 101 executed, 98 passed, 3 opt-in tests skipped, no failures.
- Shared Python host/simulator suite: 62/62 passed. Tests cover old BLE radio
  rejection before network dispatch, text limits, USB streaming/cancel,
  permissions, Pairings, protocol corruption and the other tool transforms.
- Six portable C ASan/UBSan executables passed: protocol, Markets, BLE pairing,
  Toolbox tools, cards and UI doubles. These are not physical display/audio tests.
- Swift release build with warnings-as-errors: pass.
- macOS App/DMG generation and ad-hoc codesign verification: pass (arm64).
- `git diff --check`: pass.

## Live HTTPS checks

- Portable client: all 16 smoke endpoints returned HTTP 200 and valid payloads:
  sample, time, Wikipedia, weather search/forecast, National Today, ISS, radio
  directory, BTC, ETH, gold, Brent, silver, earthquakes, currency and dictionary.
- The SDK Python initially failed all live requests because its compiled-in CA
  paths do not exist on this Mac. Using `SSL_CERT_FILE=/etc/ssl/cert.pem` passed;
  certificate verification remained enabled. No app TLS policy was weakened.
- Native URLSession: live currency, earthquakes and dictionary checks passed.
- Native USB MP3 network stream at the discovered HTTPS KLVZ source delivered
  at least 16 KiB after the original host prebuffer, then cancelled cleanly
  (10.85 seconds). This checks the helper, not USB transport or speaker output.
- Native unknown-word/404 probe timed out twice at 15 seconds. An independent
  curl request for the same word also timed out at 18 seconds without a response.
  This remains a failed live check, not a passing/skipped 404 test. Do not infer
  the precise cause from these observations. Regular dictionary lookup passed.

## Physical Flipper evidence and unresolved USB playback

Final FAP SHA256:
`dff51637b44b0288484913d15f848b9421fb93be6d731e7708a0330b08fa427b`.

Uploaded to `/ext/apps/USB/usb_internet_bridge.fap` with the official SDK storage
tool. Local/device hashes matched. Loader reported USB Internet Bridge running;
the real framebuffer showed the normal English main menu, with Test Connection,
Get Sample Text and Get Date and Time. USB still enumerated both CDC interfaces.

The latest running full FAP had **29,904 bytes free**, with a largest free block
of **25,320 bytes**, measured through the primary CLI. The radio allocation gate
needs **76 KiB total** before the original reservoirs/stack/decoder are allocated.
With no FAP running, the same device had 126,448 free bytes. Removing Bluetooth
audio did not recover enough RAM for this enlarged app's original USB decoder.

**USB speaker playback, continuous audio, Stop/Back during playback and radio
disconnect behavior are NOT certified in this build.** The admission guard must
remain; deleting it would risk an OOM fatal error, not restore a working radio.
Source restoration is complete; restoration of physically working USB radio is
not complete. No separate FAP, lower-quality USB codec or broader feature
rollback was implemented against the user's choice.

Windows/Linux native Bluetooth, GUI and binary execution were not physically
tested on those operating systems. Their shared Python implementation was tested
on macOS with mocks; local results do not certify platform hardware or CI.

## Reproduce from the repository root

```sh
export PATH="/Users/meteeeee/flipper_usb_to_wifi/venv/bin:$PATH"
ufbt
./scripts/build_sdk_example.sh
.build-tests/ble-host-venv/bin/python -m unittest discover -s tests -p 'test_*.py'
(cd macos && swift test)
(cd macos && swift build -c release -Xswiftc -warnings-as-errors)
./scripts/package_macos.sh
SSL_CERT_FILE=/etc/ssl/cert.pem .build-tests/ble-host-venv/bin/python -m scripts.live_endpoint_smoke --live
(cd macos && FIB_TOOLBOX_LIVE=1 swift test --filter ToolboxExtractorTests)
```

For the six C sanitizer commands, use the portable C test step in
`.github/workflows/build.yml`; the local macOS execution used the installed
Command Line Tools SDK/compiler and the same sources/ASan/UBSan flags.

Manual USB radio acceptance still requires enough RAM, fresh internet consent,
audible sustained playback, repeated Stop/Play, Left/physical Back, leaving the
tool/main app and unplugging USB. Do not mark those acceptance checks completed.

## Follow-up: comparison with the recorded USB baseline

The recorded `b29a8739c7ebbac9b4fa5bb80757de36fe934aa1` FAP was extracted to a
temporary directory without resetting or changing the working tree. Its SHA256
is `99db388f1754b1e8a5a93d63e40cfef869ed94e3acedfacfba8239f8d85b7069`.
It was temporarily installed at `/ext/apps/USB/usb_internet_bridge.fap` for
comparison. The real display showed the original USB menu and Toolbox. This
baseline does not include the newer Bluetooth, earthquake, currency or dictionary
menus; it is not a completed, approved source rollback.

Fresh one-connection internet consent is pending. Speaker playback on this
baseline has **not** been verified. The baseline lacks the newer radio allocation
guard; do not deliberately start it under a low-memory RPC/debugging session.

Primary-CDC CLI measurements are not idle-device measurements: official firmware
1.4.3 creates a CLI shell on DTR and frees it on disconnect. The shell stack alone
is 4 KiB. RPC screen capture adds further allocations. Do not treat the previously
reported 29,904 bytes as the exact free heap with all QA connections closed.

A private-library LTO experiment saved only about 1.3 KiB of executable sections
and was reverted; it was not delivered as a fix. The normal full-source FAP still
builds, and original USB audio DSP remains unchanged. No new audio codec, reduced
reservoir, separate Radio FAP, feature deletion or permission bypass was applied.
Source restoration alone still does not certify functional radio.

### Full-feature version restored after the comparison

The user explicitly clarified that Bluetooth Demo and the three new Toolbox
tools must remain. No full historical source rollback is authorized.

Rebuilt the current full-feature source with uFBT (target 7, API 87.1) and replaced
the temporary baseline on `/ext/apps/USB/usb_internet_bridge.fap`. Local/device
MD5 matched: `90689461dd9838124ecd4b1b2024a298`. Delivered FAP SHA256:
`5c2ceed6c4e5cdf48df1d87239a7459520a6da02579dd0865899a57922cc54fc`.

Real framebuffer captures verified Latest Earthquakes, Currency Converter and
English Dictionary in Toolbox, and Bluetooth Alpha / Requests / Pairings in Demo.
The Bluetooth toggle was left Off; no pairing records or permissions were erased.
These checks certify the restored menus, not new live data or BLE pairing tests.
Bluetooth radio remains blocked. USB radio's insufficient-memory problem remains
unresolved and must not be reported as fixed.

## USB radio RAM fix — same full-feature FAP

Moved the existing CC0 minimp3 decoder and its original mono resampler to the
desktop. The Flipper speaker output remains s16le mono at 14,493 Hz with the
same 8192-sample ring, 6144-sample prime, PWM, gain and soft knee. No feature
rollback, separate FAP, FFmpeg runtime dependency or new lossy codec was used.
The reduced admission guard reflects the actual ~21 KiB allocation, instead
of the old 76 KiB decoder/reservoir/thread budget. Windows/Linux hosts use the
same optional compiled C decoder and advertise PCM only when it is available.

USB capability bit 10, an exact request/response audio profile and even sample
length prevent old/incompatible helpers from passing MP3 bytes to the PCM
speaker. Radio alone may negotiate 64 MiB bounded segments; normal requests
remain 4 MiB and Bluetooth text 8 KiB. CRC, sequence, permission, TLS and SSRF
rules remain. Host radio queues are bounded; macOS suspends/resumes the network
source using high/low-water marks rather than repeatedly truncating live audio.

Physical testing exposed and fixed three additional faults:

- Desktop decoded blocks must be split into <=192-byte wire body chunks.
- USB RX must leave the endpoint unread while the audio consumer is blocked.
  The firmware CLI VCP uses the same callback-notification/worker-read pattern;
  draining into a 4 KiB ISR queue was dropping bytes. The queue is now removed.
  Verified against the official [1.4.3 CLI VCP source](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/applications/services/cli/cli_vcp.c)
  and the installed CDC HAL header; no SDK API was invented.
- A stale tick sampled before a lock must not see newer progress as an unsigned
  timeout. Fresh parser-clock checks and wrap-safe comparisons retain real
  timeouts. Already queued cancelled-response frames are discarded without
  playing them or producing an ERROR storm.

Current FAP SHA256:
`14b3b3e92ed78f2c2e1bfd1907869ccd35f37d1186fb37c98f44ecaec23d80e6`.
Uploaded to `/ext/apps/USB/usb_internet_bridge.fap`; local/device MD5 matched:
`4fe4b5d4b08c735430fcc1db751a2ba4`. No firmware replacement or Git push.

### Current automated results

- Main FAP and public SDK example: uFBT pass, target 7 / API 87.1.
- Swift: 105 executed, 100 passed, 5 opt-in cases skipped, zero failures.
- Shared Python host: 67/67 passed, including native C decoder fixture,
  fragmented wire chunks, extended radio-budget negotiation and cancellation.
- PCM-player and tick-race/wrap C tests: ASan/UBSan pass. Other portable parser/
  Toolbox/Pairings sanitizer results above predate this radio-only follow-up.
- Swift release build with warnings-as-errors: pass. Updated arm64 Mac App/DMG
  built and ad-hoc signature verified. No Windows/Linux physical execution claim.

### Current physical USB evidence

Real RadioBrowser search returned the US station list. The discovered KLVZ HTTPS
MP3 source was played through the normal native macOS BridgeCore, IOKit monitor,
POSIX serial transport, network policy and URLSession; GUI consent was replaced
only in the explicit operator QA harness with an in-memory Allow Once. No
Keychain, permanent USB grant or Bluetooth pairing record was changed.

The final `RadioUSBHardwareTests` run passed (97.899 seconds, including selection/
buffering). PCM progressed from 268,288 to 1,438,720 bytes at roughly 29 KB/s,
with one radio response start and zero wire errors. Physical Back returned to
the station list, cancelled the active request and stopped ongoing delivery;
the two-second post-Cancel bound passed. A preceding run after removing serial
sleeps showed the real device at `Chunks: 10667`, `Buf: 15K`, `Gaps: 0`.
Before the final cancellation-drain fix, that preceding run had 14 stale-packet
errors on Back; it was a failed check, not the final passing result.

The updated shared Python host was also physically exercised on this Mac:
Stop cancelled request 4280691016 after 306,240 PCM bytes; Play started a new
request 4280691017, while the old request's delivered-byte count stayed fixed.
The new request progressed to 820,352 bytes at ~29 KB/s before physical Back.
Both cancellation messages reached the helper, with no ERROR or device reset.

Primary CLI measured 32,064 bytes free and a 19,536-byte largest block during
native USB playback. CLI/RPC themselves allocate RAM; these are not idle-device
measurements. The latest firmware-wide minimum heap includes previous runs and
is not claimed as this build's peak usage. No RAM error or Furi reset occurred
in the final streaming/Back run.

Physical listening quality is for the user to judge; a zero underflow counter
does not prove every internet station or long-session network outage is seamless.
Actual cable removal, hour-long listening, native Bluetooth reconnection and
Windows/Linux hardware remain untested in this follow-up. Bluetooth radio is
still disabled; the other Bluetooth and Toolbox features remain in the FAP.

To repeat hardware QA, close other helpers, open USB radio on the connected
Flipper and explicitly authorize this one-session test. Select a station, play
for at least 30 seconds, then press physical Back:

```sh
(cd macos && FIB_USB_RADIO_QA=1 swift test --filter RadioUSBHardwareTests)
```

The opt-in test grants only in-memory Allow Once for hardware QA. Do not enable
it in CI or use it as the normal helper/consent UI.
