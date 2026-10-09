# Threat model

## Assets and trust boundaries

Protected assets are the computer's network reachability, login/session credentials,
cookies, local files, local-network services, user identity, and the durable
permission decision. The USB cable, every frame field, the Flipper application,
DNS answers, HTTP response metadata, and remote servers are untrusted.

The desktop host is the policy boundary. A permission grants only bounded HTTPS
proxy requests; it does not grant shell, filesystem, browser session, keychain,
Wi-Fi configuration, Internet Sharing, or arbitrary URL scheme access.

## Principal threats and controls

| Threat | MVP control |
| --- | --- |
| A random serial device is mistaken for a Flipper | OS serial metadata where available, followed by a CRC-protected HELLO, exact model family, supported version, bounded fields, and a non-empty hardware UID before any prompt |
| A different Flipper inherits permission | Persistent keys are bound to a one-way digest of device UID and protocol identity; the CLI also includes client version |
| Internet access occurs before consent | Session state rejects request messages until an explicit or stored grant is applied |
| SSRF reaches the computer or LAN | HTTPS-only URL parser, hostname restrictions, DNS resolution, block-list for non-global IP ranges, and the same validation for every redirect |
| DNS rebinding between validation and connect | Reject mixed public/private answers and revalidate redirects. The CLI connects to a validated address with the original TLS SNI; the native macOS limitation is documented below |
| Cookies or credentials leak | Isolated URLSession/direct TLS, no shared cookie/credential/cache stores, and a narrow request-header allow-list |
| Parser memory exhaustion/corruption | Fixed 28-byte header, 512-byte payload maximum, fixed buffers, checked arithmetic, both CRCs before dispatch, resynchronization on magic |
| Response exhausts Flipper RAM | Host 4 MiB cap, 192-byte chunks, fixed 1,536-byte display preview, discard/cancel after limits |
| Replay or duplicate request causes repeated network work | Non-zero request IDs are single-use within a USB session and per-direction sequences are checked |
| Unique request-ID flood grows helper memory | The native host uses one scalar last-ID; the CLI uses a bounded 64-ID replay window |
| Unplug leaves work running | Serial close/error signals cancellation, clears one-time state and closes the request transport |
| Diagnostic log leaks content | Log states, IDs, byte counts and error categories; do not log bodies, secrets, full query strings, or request headers |

## Residual risks

- The opt-in Bluetooth Alpha uses authenticated BLE pairing but does not treat
  advertisements, names or self-reported UIDs as device attestation. It never
  reuses USB persistent permissions: consent is fresh for each BLE connection.
  Its additional bridge-code recognition stores bearer tokens in Mac Keychain
  and the Flipper application's microSD directory. Physical SD access compromises
  those tokens; revoke removes app credentials, not unrelated system/phone bonds.
  See [Bluetooth Alpha](bluetooth-alpha.md) for transport and supervision limits.
- Bluetooth Internet Radio is unsupported. Helpers reject explicit audio
  requests before network dispatch and reject audio responses on BLE. The
  experimental converters/subprocesses were removed; no FFmpeg is needed.
- The native macOS host's URLSession does not expose a supported way to bind a prior
  `getaddrinfo` result to the TLS socket. DNS rebinding between validation and
  connection is reduced but not eliminated. A production-hardening phase should
  use a Network.framework transport that connects to the validated endpoint while
  preserving TLS hostname verification, or an approved outbound relay.
- `getaddrinfo` has no portable cancellation API. Request deadlines prevent a
  late DNS result from starting useful network work, but an OS-level stuck
  resolver can retain its worker until the host is restarted.
- USB CDC has no cryptographic peer authentication. Physical possession plus the
  hardware UID and explicit local prompt are the MVP identity model. A malicious
  USB device can spoof HELLO fields but still cannot bypass first-use consent.
- A persistent grant authorizes anyone who physically possesses that Flipper.
  The menu provides revocation and the grant becomes invalid on protocol identity
  changes.
- FIBP 1.0 has no application identifier. A persistent device grant can therefore
  be reused by another FAP implementing the protocol. Per-FAP grants require the
  planned application-identity field in a future protocol minor version.
- The helper is intentionally not sandboxed in the command-line SwiftPM MVP;
  distribution should add signing, hardened runtime, sandbox entitlements review,
  and notarization.
## Independent connection ownership

Internet Bridge has explicit USB and Bluetooth entry points. macOS holds two
separate coordinators, network clients and prompt owners. A tab selection is
presentation only; stopping/revoking a channel cannot grant, cancel or dismiss
the other channel's permission. USB stored grants never authorize Bluetooth.
See [connection modes](connection-modes.md). This supersedes historical Alpha
navigation, not the BLE encryption/code or HTTPS/SSRF limitations above.
