# Full local QA run — 2026-10-04

This records the latest test run, not a release certification. Production code
was not changed to make failed checks pass. Existing local v0.5/Alpha changes
were preserved; no source commit, push, release or Catalog update was made.

## Environment

- macOS 27.0 (26A428), arm64; Xcode 16.4.
- uFBT official SDK 1.4.3, Target 7 / API 87.1.
- Physical Mico Flipper runs Momentum mntm-012, not stock firmware.
- Bridge CDC: `/dev/cu.usbmodemflip_Mico3`; primary CLI/RPC CDC:
  `/dev/cu.usbmodemflip_Mico1`.
- The Mac was locked. Native consent/menu interaction was unavailable.
  Hardware USB checks therefore used the existing Python helper with the
  user's explicitly authorized once-only permission for this Mico. An isolated
  QA permission path was used and no persistent permission file was written.

## Results

| Check | Result | Boundary |
| --- | --- | --- |
| Python suite | 45/45 passed | Includes PTY/real pyserial integration, framing, permission and network policy tests |
| Swift suite with live providers enabled | 77/78 passed, one failed | No skipped tests; unknown-word dictionary request timed out |
| C ASan + UBSan | All five groups passed | Protocol, Markets, Toolbox tools/cards/UI; UI uses host doubles |
| Main FAP and SDK consumer | Build/import checks passed | Target 7 / API 87.1 |
| Swift release warnings-as-errors build | Passed | Native arm64 build |
| macOS app/DMG packaging | Passed | Ad-hoc signature verified; not notarized |
| Portable live HTTPS smoke | 16/16 passed | No serial connection or consent changes |
| Physical USB HELLO and once-only approval | Passed | Python helper, real Flipper |
| Physical denied access | Passed | Denied screen; no REQUEST_START observed in denied phase |
| Physical PING/PONG, sample and time GET | Passed | Real framebuffer and serial frames |
| Physical earthquakes | Passed | Four response chunks; card navigation through 10/10 |
| Physical currency selectors and two-way amounts | Passed with issue below | USD/EUR and ZAR/USD; 21 choices; 10 ZAR → 0.60 USD, then 10 USD → 166.22 ZAR |
| Physical dictionary | Passed for `hello` | 1/4 definition and 4/4 source/license pages verified |
| Physical BTC Markets refresh and Back | Passed with issue below | Repeated request IDs and updated timestamp; no new price requests after Back |
| Physical ISS and National Today | Passed | Live formatted content displayed |
| Physical FAP exit/relaunch | Passed | Back returned to firmware browser; reinstall/relaunch completed |
| Bluetooth Alpha | Not passed | Enable did not remain On; recovery was not immediate; wireless pairing/HTTPS untested |

Live smoke sources: sample text, time, English Wikipedia, Open-Meteo search and
forecast, National Today, ISS, compatible HTTPS MP3 radio directory, BTC, ETH,
gold, Brent BZUSDT, silver, earthquakes, USD/EUR and dictionary `hello`.
These network checks do not certify all corresponding Flipper UI paths.

## Failures and observations

### Portable helper cancel/re-entry bug (reproduced)

Rapidly changing currency pairs cancels request A and sends request B before A's
network worker exits. `HostSession` retains `active` until worker completion, so
B receives ERROR. The converter displayed Error. Retrying after the old worker
finished produced a valid ZAR/USD rate.

The worker can also enqueue response frames after cancellation. This was observed
again when leaving a refreshing BTC screen: CANCEL was followed by stale response
frames and client ERRORs. No further price requests were sent after Back.

A deterministic, zero-network reproduction also failed the expected immediate
re-entry behavior: HELLO → Allow Once → REQUEST_START/END(42) → CANCEL(42) →
REQUEST_START(43). The cancellation event was set, but `active` remained occupied
and request 43 received ERROR. Existing 45 Python tests do not cover this race.
This finding is about the portable Python helper, not proof of the same bug in
the Swift helper. No production fix was applied in this test-only request.

### Live unknown-word dictionary failure

`ToolboxExtractorTests.testLiveUnknownDictionaryWordPreserves404` timed out for
`zzzznotawordxxxx`. A direct independent HTTPS request to the same provider also
returned HTTP 522 after about 20 seconds. The provider failure is observable
outside the bridge, but a live 404 was not verified and this test remains failed.
Offline 404 handling checks passed.

### Bluetooth Alpha / USB recovery

After exercising Toolbox, a headless RPC Right key was sent in Demo without
starting a framebuffer stream. The toggle remained Off and Connection Info
showed USB disconnected/host waiting. The implementation can reject profile
startup because of its heap guards or profile startup failure; the framebuffer
does not establish which branch caused this attempt to fail.

A fresh terminal helper alone did not immediately restore the handshake. After
exiting/reinstalling/relaunching the FAP and reopening the helper, retrying Test
Connection completed HELLO, permission and PING/PONG. This recovery path was
verified, but seamless Alpha-failure-to-USB recovery was **not** passed. Do not
publish Alpha as hardware-validated on the strength of its unit tests.

## Not tested / blocked

- Actual BLE OS permission, PIN pairing, allow/deny, wireless HELLO/PING/GET,
  cable-unplugged proof, repeated successful On/Off cycles and BLE range loss.
- Native Mac permission buttons/menu interaction while the computer was locked.
- Physical USB cable removal, since the operator was remote.
- Radio audio playback, listening quality, sustained memory/load tests and
  radio Back during playback on this enlarged Alpha build. Directory success is
  not audio certification.
- Exhaustive real-device Wikipedia/Weather/search typing, every currency/coin,
  and every provider failure. Automated and live network coverage is distinct.
- Windows/Linux native binaries and physical host/device behavior.

## Re-run commands

From the real repository root:

```sh
.host-venv/bin/python -m unittest discover -s tests -p 'test_*.py' -v
.host-venv/bin/python -m scripts.live_endpoint_smoke --live
(cd macos && FIB_TOOLBOX_LIVE=1 swift test --scratch-path .build-tests-swift)
(cd macos && swift build -c release --scratch-path .build-qa-warnings -Xswiftc -warnings-as-errors)
/Users/meteeeee/flipper_usb_to_wifi/venv/bin/ufbt
UFBT=/Users/meteeeee/flipper_usb_to_wifi/venv/bin/ufbt ./scripts/build_sdk_example.sh
./scripts/package_macos.sh
git diff --check
```

The default Xcode clang/SDK combination failed to link on this machine, and the
older Xcode ASan runtime hung before `main`. The matching CLT compiler/linker/SDK
passed all five sanitizer groups. For example:

```sh
mkdir -p .build-tests
/Library/Developer/CommandLineTools/usr/bin/clang \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -B/Library/Developer/CommandLineTools/usr/bin \
  -mmacosx-version-min=13.0 -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
  bridge_protocol.c tests/test_bridge_protocol.c -o .build-tests/all_protocol_san
./.build-tests/all_protocol_san
```

The same flags were used for `markets.c tests/test_markets.c`,
`toolbox_tools.c tests/test_toolbox_tools.c`,
`toolbox_tools.c toolbox_cards.c tests/test_toolbox_cards.c`, and
`toolbox_tools.c toolbox_cards.c toolbox_ui.c tests/test_toolbox_ui.c`
(the UI group additionally needs `-Itests/ui_stubs`).

Upload to the explicit primary CDC if dual ports confuse uFBT auto-discovery:

```sh
/Users/meteeeee/flipper_usb_to_wifi/venv/bin/ufbt launch FLIP_PORT=/dev/cu.usbmodemflip_Mico1
```

Final upload completed all 14 chunks (100%) and launched successfully. The
matching `dist/usb_internet_bridge.fap` SHA256 is
`929a90581ed68745170a0a00079bab24ce631c775cdaef21029c9bd3f8f8053b`.
USB/default-Off was left active. Temporary once-allow helper was shut down;
normal native Mac helper was restarted without an automatic-consent option.

Real framebuffer evidence is local under ignored `.build-tests/`, including:
`all-tests-usb-ping-confirmed.png`, `all-tests-earthquakes-10.png`,
`currency-ten-zar.png`, `currency-ten-usd.png`, `dictionary-hello-real.png`,
`dictionary-source-real.png`, `markets-btc-refresh-a.png`,
`markets-btc-refresh-b.png`, `iss-real.png`, `national-today-real.png`,
`alpha-on-hardware.png`, `fap-exit-cleanup.png`, `final-usb-verified.png`.
Intermediate editor/navigation captures are not passed checks merely because
their filenames contain words such as “valid” or “confirmed”.
