# Internet Bridge: connection modes

The display name is **Internet Bridge**. The first Flipper screen offers
**USB Internet Bridge** and **Bluetooth Internet Bridge**, not a Demo toggle.

## Ownership and lifecycle

```text
                     Internet Bridge (selector)
                       /                 \
              USB Internet Bridge   Bluetooth Internet Bridge
              CDC transport         GATT transport + recognition
                       \                 /
                        FIBP + HTTPS tools

Mac: USB panel -> USB coordinator -> own HTTPS client + consent
     BLE panel -> BLE coordinator -> own HTTPS client + consent
                                 + code recognition / Keychain credentials
```

One Flipper session owns exactly one selected transport. No bridge transport
starts before selection. Back from either mode's main menu cancels requests,
stops audio, joins workers, clears session consent and returns to the selector.
Back again exits. Leaving Bluetooth restores the ordinary firmware profile.
There is no automatic fallback to a different transport on failure.

USB has Test Connection, Get Sample Text, Get Date and Time, Toolbox, Custom URL
Request and Connection Info. Bluetooth starts with Test Connection, Connection
Requests and Pairings, followed by Get Sample Text, Get Date and Time, Toolbox,
Custom URL Request and Connection Info. USB menu order is unchanged.
USB keeps all nine Toolbox tools; Bluetooth keeps the text tools but hides
Internet Radio. USB's bounded host-decoded PCM speaker pipeline is unchanged.

Select an incoming computer in Connection Requests, then explicitly Pair or
Connect. A new computer requires the bridge code. Recognized computers do not
need a new code, but internet permission is still requested on every connection.
Known Computers -> Revoke removes recognition, not normal internet consent.

## Native macOS panel

USB and Bluetooth tabs only select the visible controls. Their switches
independently start/stop each connection. USB monitoring starts on launch;
Bluetooth scanning starts only when enabled. Each owns its coordinator, HTTP
client, permission prompt, active request and lifecycle generation. Late
callbacks from a stopped session cannot overwrite its replacement.

One USB and one Bluetooth device can use the Mac simultaneously (one request
per transport). Disconnect, Cancel Request and Revoke Access target only that
channel. Bluetooth Reconnect does not pause USB.

AppKit has one modal stack. An owner-scoped FIFO serializes code, consent and
revocation alerts. Cancelling a USB prompt cannot abort a Bluetooth prompt.
The compact status/management panel uses SwiftUI; permission alerts use AppKit.

Pairings lists recognized Flippers. Revoke Pairing deletes a bridge credential,
not macOS system bonds or USB grants. Offline revocation cannot instantly edit
the other device; the next recognition exchange reconciles stale records and
requires a new code.

## Compatibility and security

The FAP appid/output filename `usb_internet_bridge`, executable/module name,
bundle identifier `com.flipperusb.internetbridge`, BLE UUIDs and credential
storage namespaces remain unchanged. Renaming does not create a second app
identity or discard existing consent/pairing records. Internal Alpha identifiers
remain for source/wire compatibility; they are no longer product UI labels.

USB retains Allow Once / Always Allow / Deny. Bluetooth uses a separate
in-memory consent store and Allow Once / Deny only, even if that Flipper has a
saved USB grant. Pairing is not internet permission. TLS, SSRF, cookies and
credential isolation remain unchanged. Bluetooth text is capped at 8 KiB;
radio is USB-only. Leaving Demo is a navigation change, not universal hardware,
performance or security certification.

Windows/Linux hosts retain their terminal interface and legacy Bluetooth opt-in
commands. This redesign does not turn them into native GUI apps.
Historical notes: [bluetooth-alpha.md](bluetooth-alpha.md). This document
supersedes their Demo navigation and single-helper transport switching.

## Build and check

From the source repository root:

```sh
ufbt
(cd macos && swift test -Xswiftc -warnings-as-errors)
./scripts/build_sdk_example.sh
./scripts/package_macos.sh
open "dist/macos/Internet Bridge.app"
```

Run only one helper per transport. Close the old helper before opening the new
build. No system network settings or OS pairing lists are changed.

Regression checklist: selector -> USB -> Back -> Bluetooth -> Back -> USB;
Deny/Allow Once; code pairing/reconnect/revoke; disconnect during a request;
USB radio Play/Stop/Back; exit. Automated tests do not certify audible playback
or actual native Bluetooth pairing. See the dated test record for results.
