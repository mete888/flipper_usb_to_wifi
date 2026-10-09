# BLE bridge recognition extension (Alpha, local/unreleased)

FIBP v1's CRC framing is retained. This extension applies **only** to the private
BLE Alpha profile after native authenticated/encrypted GATT is established.
USB FIBP and USB permission persistence do not use it. HELLO capability bit 5
(`0x20`) signals recognition and bit 6 (`0x40`) signals physical request
selection. The helper requires both: older automatic-code FAPs fail closed.

Every extension frame uses major=1, minor=0, flags=0, request_id=0, sequence=0.
Extension frames do not consume ordinary FIBP control sequence numbers. Multi-byte
integers are little-endian. The nonce is Flipper's current HELLO client_nonce.

| Type | Direction | Payload bytes |
| --- | --- | --- |
| `0x40 PAIR_OPEN` | Mac → Flipper | nonce:u64 + host_id:16 + token:32 = 56 |
| `0x41 PAIR_CODE` | Mac → Flipper | nonce:u64 + host_id:16 + ASCII digits:6 + new_token:32 = 62 |
| `0x42 PAIR_NEEDED` | Flipper → Mac | nonce:u64 + status:u8(2) = 9 |
| `0x43 PAIR_RESULT` | Flipper → Mac | nonce:u64 + status:u8(0=reject,1=accept) = 9 |
| `0x44 PAIR_REVOKE` | Mac → Flipper | nonce:u64 + host_id:16 = 24 |
| `0x45 PAIR_WAITING` | Flipper → Mac | nonce:u64 + status:u8(3) = 9 |
| `0x46 PAIR_FORGOTTEN` | Flipper → host | nonce:u64 + host_id:16 = 24 |

1. Flipper sends ordinary HELLO with a fresh random nonce and capability bits 5/6.
2. Mac sends OPEN with its random installation host ID. Flipper checks the
   token but **does not accept or generate a code**, even for a recognized host.
   It sends WAITING and exposes the candidate in Demo > Requests (the
   Connection Requests screen).
   The user selects the candidate and Pair (new host) or Connect (recognized
   host). Reject/Back on its detail screen releases the connection. Waiting
   expires after 120 seconds; selection uses the expected nonce to avoid stale
   UI approving a different peer. Recognized Connect sends acceptance; new Pair
   generates/displays six digits and sends NEEDED (never the code). This starts
   a separate 120-second code-entry lifetime. Premature CODE is rejected.
3. The helper asks the user to type the Flipper code. CODE carries six ASCII
   digits and a new random 256-bit token. Flipper checks nonce/host/code,
   limits to three attempts, and persists the token before sending acceptance.
   Wrong attempts 1/2 return NEEDED; attempt 3, expiry, or save failure rejects.
4. Mac commits its token to Keychain only on acceptance. Only then does the
   normal coordinator receive HELLO and send HELLO_ACK / internet permission
   prompt. Every BLE connection asks Allow Once/Deny regardless of pairing.
5. Mac revoke removes its own credential/record and closes the session. REVOKE
   additionally removes Flipper's matching app credential if active/deliverable.
   If disconnected, the next OPEN has a zero token and requires code regardless
   of the stale Flipper record. Successful pairing replaces that host's record.
6. Flipper Demo > Pairings lists only valid app credential files, eight per
   page. Revoke deletes exactly the selected host's record. If that host is
   active, revoke clears recognition/permission before sending FORGOTTEN and
   disconnecting. Helpers validate its full current nonce + host before deleting
   their matching token/row. If the helper is offline, its token is rejected on
   reconnect; first NEEDED removes stale helper recognition before showing code.

"Mac" in the original extension directions includes Windows/Linux BLE Demo
hosts using the identical frames. They require fresh one-time internet consent
and the same GET-only / 8 KiB text cap. Optional radio capabilities are described
in [Bluetooth radio](bluetooth-radio.md). OS bonds are never changed by bridge revoke.

Token/host/code comparisons on Flipper avoid early exits. Replay is rejected
using fresh nonces and current state; strict lengths, flags and IDs are checked.
Ordinary HELLO_ACK, permission and network frames are ignored on BLE before
recognition, including when sent by an older helper. No code/token is logged.

## Storage and security boundary

There is one incoming candidate, not a peripheral-side scan of every nearby Mac.
The standard Flipper FAP profile is the BLE peripheral; the Mac is the central.
The label `Bridge PC - AB12` uses the first two bytes of the random host ID,
not a personal hostname/username. It is an unauthenticated display hint and can
collide or be spoofed; native link authentication and the code/token remain the
security boundary. OS Bluetooth PIN prompts may precede this application-level
selection on the first link. Their timing/security is not changed by this FAP.

Mac: sanitized UUID/name metadata and random 16-byte installation ID in preferences;
80-byte credential (host:16 + token:32 + SHA256(device UID):32) in Keychain. A
UID change or reset of host identity requires another code. Legacy v1 observed
records cannot authorize a session. No cookies, usernames, or Wi-Fi credentials
are used. Tests inject an in-memory vault, never the operator's Keychain.

Windows/Linux: optional Bleak backend + secure OS keyring; 80-byte secret =
host ID:16 + SHA256(tag/id-type/device UID):32 + token:32. Metadata holds the
random host ID, sanitized Flipper name and digest. Null/plaintext/chained
keyrings fail closed. Permission files for USB are separate and ignored on BLE.

Flipper: `/ext/apps_data/usb_internet_bridge/ble_pairs/<hex-host-id>.key`.
Record = `FBP2` + host:16 + token:32 + CRC32:4 (56 bytes). A synced temporary
file is renamed into place; corrupt/truncated/unreadable records cannot resume.
Power loss during replacement can require re-pairing. Only this application's
exact credential path is changed; firmware/phone bond files are never deleted.

CRC detects corruption, **not authenticity**. Bearer tokens rely on the native
authenticated/encrypted GATT link. The code is a bridge recognition step, not a
replacement for native BLE authentication and not a new forced OS PIN. Reading
the Flipper SD or controlling either endpoint compromises recognition. The
three-attempt bound is per connection, not a global attack-rate guarantee.
Credential file count on Flipper is not yet globally bounded; physical users
who authorize many distinct host installations should remove obsolete app
credentials through Demo > Pairings. Public catalog release requires hardware validation.
