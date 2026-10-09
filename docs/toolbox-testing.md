# Toolbox v0.5 validation

For the subsequent Wikipedia/Weather/National Today/ISS visual refresh and
Windows/Linux graphical host, see [the 2026-10-08 UI report](test-results-2026-10-08-ui.md).

For the subsequent full run (including physical card navigation, two-way
amount editing, live test failure and portable-helper cancellation bug), see
[the 2026-10-04 full QA report](test-results-2026-10-04.md). Its results supersede
the earlier local validation record below where they overlap.

Use the matching v0.5.0 FAP and desktop helper. All three new tools appear after
Markets; existing Toolbox items keep their order. The companion performs the
HTTPS requests. No network request is permitted before device approval.

## Automated checks

From the repository root:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
mkdir -p .build-tests
clang -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  toolbox_tools.c tests/test_toolbox_tools.c -o .build-tests/test_toolbox_tools
./.build-tests/test_toolbox_tools
clang -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  toolbox_tools.c toolbox_cards.c tests/test_toolbox_cards.c -o .build-tests/test_toolbox_cards
./.build-tests/test_toolbox_cards
clang -std=c11 -Wall -Wextra -Werror -pedantic -Itests/ui_stubs -I. \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  toolbox_tools.c toolbox_cards.c toolbox_ui.c tests/test_toolbox_ui.c -o .build-tests/test_toolbox_ui
./.build-tests/test_toolbox_ui
cd macos
swift test
```

Python and Swift share JSON fixtures in `tests/fixtures/toolbox`. Checks cover
latest-first earthquake ordering, excluding non-earthquakes, ASCII conversion,
bounded output, definitions and source/license attribution, precise currency
wire fields, invalid/truncated JSON, oversized source data and invalid rates.
Portable C tests cover amount and word validation, URL building, mismatched
currency pairs, every truncated currency reply and small output buffers.
Card tests check structured earthquake/dictionary fields, attribution,
truncated input and forward/reverse conversion, preserving the last edited
amount, invalidating stale rates and same-currency/zero amounts.
The UI test uses host doubles to check all four converter actions, callbacks
after model unlock, page bounds and scrolling limits. It does not reproduce
the firmware scheduler or actual Flipper font metrics.

External API tests are skipped by default so CI does not depend on provider
availability. To send real HTTPS requests through the macOS network client:

```sh
cd macos # omit if already in macos
FIB_TOOLBOX_LIVE=1 swift test --filter ToolboxExtractorTests
```

These checks include successful USGS, Frankfurter and dictionary responses,
bounded ASCII output and a dictionary HTTP 404. Provider outages, rate limits,
DNS failures and timeouts can fail live tests without failing offline tests.

## Physical-device checklist (not replaced by automated checks)

1. Launch the updated helper and FAP. Deny permission: none of the three tools
   should open a keyboard or send a request. Then grant access and try again.
2. Latest Earthquakes: check ten events, magnitudes, UTC times, locations and
   depths against the [USGS query](https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&orderby=time&limit=10&eventtype=earthquake).
   Use Left/Right to change cards, Up/Down for a long location and OK to refresh.
   The padded `M 4.1` / depth fields must not overlap their borders or location;
   the location card spans the screen, with UTC time and USGS on the bottom row.
   The top counter must fit `10/10`. A rail appears only for a long location.
   An empty feed must show an explicit empty result.
   USGS reporting is not exhaustive, and this is not an alerting system.
3. Currency Converter: highlight the left/right selectors with the arrows and
   choose USD / TRY with OK. Edit the left amount to `100.50`. Compare the result
   against `100.50 * rate` from the [rate endpoint](https://api.frankfurter.dev/v2/rate/USD/TRY).
   Then edit the right amount and verify division by the same rate updates the
   left field. Change either selector: it must not reuse the previous pair's
   rate. Check the provider date. Try same-currency conversion and zero. Letters,
   negative values and more than two decimals must fail input validation.
   Both selectors must also offer HKD, SGD, NZD, DKK and ZAR (21 options total).
   Rates are reference rates, not real-time exchange/trading prices.
4. English Dictionary: try `hello`, a hyphenated word, and a nonexistent word.
   Check the separate word/part-of-speech heading, Left/Right meaning pages,
   Up/Down scrolling and the final source/license attribution page. OK starts
   a new word search. A nonexistent word must show `Word not found`, not raw JSON.
   The reader panel should have clear gaps around the part of speech and its
   three definition rows. A rail shows scroll position only for longer entries;
   no permanent control legend should crowd the text.
5. Back from either converter editor/selector returns to the four-field screen. Back
   from either other tool returns to Toolbox. Back during a request must cancel
   it without a late result reopening the tool.
6. Unplug USB during each request. The existing connection-loss path must
   cancel access. Reconnect and retry without restarting either application.
7. Exercise Wikipedia, Weather, National Today, ISS, Radio and Markets as a
   regression check, especially Back while radio is playing.

The text output is capped at 1400 bytes; helper source JSON is capped at 64 KiB.
Definitions are deliberately shortened. TLS, SSRF checks, permissions, timeouts
and USB chunking continue to use the existing bridge layers.

## Local validation record (2026-10-04)

- uFBT FAP build and import checks passed for Target 7 / API 87.1.
- 45 Python tests passed, including the POSIX virtual-serial integration test.
- Swift ran 72 tests: 70 passed, two live-provider tests skipped in offline mode.
- Protocol, Markets and Toolbox C tests passed with AddressSanitizer and UBSan.
- New card-model and host-double UI input/scrolling tests also passed with
  AddressSanitizer and UBSan; these do not certify real-device navigation.
- Actual macOS and Python network clients received HTTP 200 and compact ASCII
  payloads from all three providers. The live unknown-word check timed out;
  another nonexistent-word query returned upstream HTTP 522. HTTP 404 handling
  is covered by offline tests, but a real 404 was not verified in this run.
- The updated live macOS test fetched exactly ten USGS earthquake records,
  within 1400 ASCII bytes, and also passed the rate/dictionary provider checks.
- The release-mode arm64 macOS app and DMG built successfully; the local ad-hoc
  application signature verified. This is not Developer ID notarization.
- The v0.5.0 FAP was uploaded to `/ext/apps/USB/usb_internet_bridge.fap` and
  launched on a real Flipper. USB re-enumeration caused the expected uFBT
  `Device not configured` after launch; the updated helper then detected the
  device and presented its permission prompt. The user granted access, and the
  real Flipper displayed the two-selector/two-amount USD/EUR screen with a live
  result and provider date. The latest earthquake/dictionary spacing refinements
  and expansion to 21 currencies were subsequently uploaded successfully
  (all 13 chunks, 100%) and launched at `/ext/apps/USB/usb_internet_bridge.fap`.
  The dual CDC ports reappeared and the running helper opened the bridge port.
  The local `dist` FAP matches the exact binary sent by uFBT launch.
  Earlier real-screen/navigation checks were blocked when the
  primary CDC screen-capture session stopped responding; Mac UI testing was also
  unavailable because the Mac was locked. Earthquake/dictionary card navigation
  and two-way converter editing on hardware therefore remain unverified.
- The subsequent ten-event/card-panel FAP and matching rebuilt macOS helper
  were installed and started. The real Flipper capture showed `Waiting for
  permission`; approval is left to the user. Final rounded card appearance on
  hardware remains pending, rather than being certified by host-double tests.
- Windows/Linux helper source and shared transformations are updated; their
  native binary builds and physical USB behavior were not validated on macOS.

No GitHub release or Apps Catalog manifest was changed by this local update.
