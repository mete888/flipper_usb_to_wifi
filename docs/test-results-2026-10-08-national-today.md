# National Today follow-up — 2026-10-08

## Changes

- Removed the generic subtitle; four body lines now fit beneath the header.
- Wrapping and painting use the same SDK `canvas_glyph_width` advances. Summing
  single-character string bounding boxes previously underestimated real text.
  The SDK API was checked in the installed API 87.1 headers and official 1.4.3
  `applications/services/gui/canvas.c` implementation.
- Both helpers preserve spaces around bold tags, including attributed tags.
  Bold text within a word does not introduce artificial spaces. Marker-only
  paragraphs are rejected rather than opening an empty reader.
- Responses exceeding the 1,536-byte preview explicitly end with
  `[Text shortened]`; the buffer was not increased. Truncation indication and
  marker handling have sanitizer coverage, not a fabricated live response.
- No radio, Bluetooth transport, pairing, permission policy or network-policy
  changes. Shared reader advance correction also applies to Wikipedia.

## Validation

- uFBT FAP/import check passed: Target 7, API 87.1.
- Portable C UI tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
  The font double now distinguishes glyph advance from painted bounding width
  and asserts that body glyphs do not enter the scrollbar/right padding.
- Python: 88 tests passed, including extractor spacing/empty-paragraph cases.
- Swift: 111 executed, 105 passed, six opt-in live/hardware tests skipped;
  warnings treated as errors, zero failures.
- macOS release app/DMG built and signature verification passed. Installed app
  retains `com.flipperusb.internetbridge`; the previous installed app is
  recoverable under `.build-tests/macos-backups.noindex/national-today.MvDqZs/`.
- Previous on-device FAP was copied to
  `.build-tests/fap-before-national-today.DBQlig/usb_internet_bridge.fap`, MD5
  `7e0722ecfc211465adc73db3e78bfe48`.
- Updated FAP uploaded to `/ext/apps/USB/usb_internet_bridge.fap`, 107,012 bytes;
  local/device MD5 matched `0546e4622cc6f336a69ca41d6b6115e9`.
  Closing the previous dual-CDC app re-enumerated USB, so the official uploader
  reported `Device not configured` after transfer. Reopening the primary port,
  comparing the SD-card MD5 and launching the app confirmed successful installation.
- Physical Flipper with the updated native Mac helper fetched the live daily
  paragraph. Actual framebuffer checks confirmed four visible lines, wrapped
  bold names without right-edge clipping, Down scrolling, reaching the final
  text at `46/46`, Left navigation, OK refresh returning to the first position,
  and Back returning to Toolbox without reopening the reader. Screen RPC was stopped before USB mode
  selection; no firmware USB lock was bypassed.

Actual nearest-pixel captures (not simulated artwork):

- `.build-tests/national-today-fixed-live.png`
- `.build-tests/national-today-fixed-scroll.png`
- `.build-tests/national-today-fixed-end.png`
- `.build-tests/national-today-fixed-refresh.png`
- `.build-tests/national-today-fixed-back.png`

Windows/Linux extractor tests ran on macOS; no target-platform VM or real BLE
device test is claimed. No Git commit, push, release or Catalog changes.

## Subsequent Tools-category migration

The same source was rebuilt with `fap_category="Tools"`; uFBT Target 7/API 87.1
import validation passed. README paths, latest UI details and test counts were
updated in English. The rebuilt FAP was uploaded to
`/ext/apps/Tools/usb_internet_bridge.fap`; local/device MD5 matched
`feddd492ff77b848f6eb347212773deb`. Only after verifying that copy and backing up
the previous USB FAP to `.build-tests/fap-before-tools.ztzg8S/usb-previous.fap`
was `/ext/apps/USB/usb_internet_bridge.fap` removed. The device now has one copy,
under Tools. App ID, filename, permissions and pairing data were not changed.
This is a local/device migration, not a published Catalog update.

The subsequent display-name-only update uses **Internet Bridge** on Flipper;
desktop helpers retain **Flipper Internet Bridge**. uFBT import validation
passed, the new Tools FAP's local/device MD5 matched
`c9267562635315fa043ace9c1ef25923`, and the device reported
`Application "Internet Bridge" is running`. The previous FAP remains recoverable
under `.build-tests/fap-before-display-name.811viO/`. No protocol, app ID,
pairing, network or desktop application changes were made for this rename.

## Reproduce

```sh
ufbt
python -m unittest tests.test_host_transforms -q
swift test --package-path macos -Xswiftc -warnings-as-errors
clang -std=c11 -Wall -Wextra -Werror -pedantic -Itests/ui_stubs -I. \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  toolbox_tools.c toolbox_cards.c toolbox_ui.c tests/test_toolbox_ui.c \
  -o .build-tests/test_toolbox_ui_nt
./.build-tests/test_toolbox_ui_nt
./scripts/package_macos.sh
```

On this Mac, sanitizer tests used the matched Command Line Tools clang and
MacOSX27.0 SDK. Full Python radio tests also require the host-only MP3 fixture
described in the previous UI validation report; no tone was played on Flipper.
