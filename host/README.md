# Cross-platform host

The compact desktop host runs on Windows 10/11 and Linux. It mirrors the native
macOS panel: USB / Bluetooth tabs, independent connection switches, permission
status, Pairings, Cancel Request, and Diagnostics. The native SwiftUI menu bar
application remains the recommended macOS experience. The CLI is also retained.

## Desktop GUI

From the repository root, in a Python virtual environment:

```sh
python -m pip install ".[desktop]"
fib-bridge-desktop
```

Use `fib-bridge-desktop --dark` for the dark panel. The GUI adds
PySide6-Essentials (Qt Widgets), Bleak and keyring; the minimal CLI does not
require Qt. See [dependency notices](THIRD_PARTY_DESKTOP.md).

Select USB or Bluetooth to view its controls. Switching tabs does **not** stop
the other connection or transfer its permission. USB is enabled initially;
Bluetooth is opt-in. Select this computer on Flipper's **Bluetooth Internet
Bridge → Connection Requests** screen. A new computer requires the six-digit
bridge code, then a separate Allow Once / Deny internet prompt. Recognized
computers still need fresh internet consent. USB may remember Always Allow;
Bluetooth cannot inherit it.

**Revoke Access** cancels internet access only. **Pairings → Revoke Pairing**
deletes recognition and requires a new code next time; it does not remove USB
grants or system Bluetooth bonds. Closing code/consent is cancellation/denial.
Disconnect or switch-off dismisses that connection's pending dialogs. Quit ends
both connections. Closing the panel hides it only when a system tray exists;
otherwise it quits. The GUI prevents a second GUI instance for the same user.
Do not run the CLI/native Mac helper against the same device concurrently.

Real-widget previews are in [screenshots/desktop](../screenshots/desktop).
They are rendered on the development Mac with labelled fixture data, **not**
evidence of Windows/Linux hardware validation. The target-platform CI captures
the same widgets but has not been dispatched for this working-tree change.

Use the v0.5.0 host with the v0.5.0 FAP for Latest Earthquakes, Currency Converter
and English Dictionary. These endpoint responses are converted into bounded
ASCII display payloads by the helper. Upgrading just the FAP is not sufficient.
Provider details and tests are in [Toolbox validation](../docs/toolbox-testing.md).

## Install from source

Install Python 3.10 or newer, then run from the repository root:

```sh
python3 -m venv .host-venv
```

Linux/macOS:

```sh
. .host-venv/bin/activate
python -m pip install .
fib-bridge
```

Windows PowerShell:

```powershell
.host-venv\Scripts\Activate.ps1
python -m pip install .
fib-bridge
```

The helper detects Flipper serial ports. Use `fib-bridge --list-ports` and
`fib-bridge --port PORT` if automatic discovery cannot select the device.

The first HELLO displays a terminal permission prompt. An always-allow choice
stores only a SHA-256 permission identifier in the current user's configuration
directory. Run without administrator/root privileges; on Linux the user may
need normal serial-port membership (commonly the `dialout` group).

## Optional terminal Bluetooth interface

USB remains the CLI default. For compatibility, `--demo` still opens its small
terminal Demo menu: connect Bluetooth, list Pairings, revoke a bridge pairing,
exit. The desktop GUI above is the preferred Windows/Linux interface.
This is BLE GATT, not Bluetooth Classic SPP or USB Ethernet.

Install the optional dependencies from the repository root:

```sh
python -m pip install ".[bluetooth]"
fib-bridge --demo
```

Direct commands work in both PowerShell and Linux shells:

```sh
fib-bridge --bluetooth-demo
fib-bridge --pairings
fib-bridge --revoke-pairing EXACT_ID_FROM_PAIRINGS
```

Do not run two bridge hosts against the same Flipper concurrently. Enable
Flipper **Bluetooth Internet Bridge**, then enable Bluetooth in the GUI or
start the terminal host. Complete any native OS Bluetooth pairing first.
Flipper **Connection Requests** shows
`Bridge PC - AB12` using the helper's random installation ID, not your username
or hostname. Select Pair (new host) / Connect (known host). Only Pair generates
a bridge code; enter its six digits in the terminal (empty entry cancels).
Recognition does **not** grant internet access: each session asks Deny / Allow
once. There is no Always Allow for BLE; USB grants never carry over. Use Ctrl+C
to stop; denied/expired/disconnected sessions are not silently reconnected.

**Flipper Bluetooth Internet Bridge → Pairings** lists recognized computers (eight per page).
Select one, confirm Revoke, and only its app-owned credential is deleted.
An active host receives a nonce/host-bound FORGOTTEN notice and ends internet
access; its own pairing row/token are removed. Offline hosts cannot receive
that notice, but their old token no longer works: the next Pair requires code
and removes stale recognition before re-pairing. Flipper OS/phone bonds and
USB internet grants are untouched. Desktop `--revoke-pairing` deletes the local
token and row while this CLI is disconnected; Flipper cannot be edited remotely
while offline, but the next OPEN sends no token and requires Pair/code.

Tokens are stored in a secure OS keyring: Windows Credential Manager; Linux
Secret Service or KWallet. A working **user-session Bluetooth service/agent**
and unlocked user keyring are required; no root/administrator helper is added.
On headless Linux without these services, Demo fails closed. Plaintext, null,
and automatic chained keyring backends are rejected. If multiple secure
keyrings are installed, select the intended backend through keyring's normal
configuration rather than falling back to a file. JSON metadata contains only
sanitized display names, UID hashes and a random host ID, never the token/code.

BLE Demo supports HTTPS GET only, an 8 KiB text response cap, acknowledged GATT
writes up to 128 bytes when ATT MTU permits (20-byte fallback), receive credits,
bounded queues, cancellation and timeout.

Internet Radio is USB-only. The Bluetooth radio experiment and its converters
have been removed. Bluetooth helpers reject audio requests before network
execution, including requests from an older experimental FAP. No FFmpeg is
required. Text tools, manual Pair/Connect, Pairings and per-session consent
are unchanged.

USB radio uses the bundled optional C extension (`minimp3`, already vendored),
not FFmpeg. Installing from source needs a C compiler and Python development
headers; release binaries include the decoder. Run `python -m pip install .`
again after updating. Without the extension, text/Bluetooth still work but the
helper does not advertise USB PCM and the FAP asks to update the helper. The
decoded stream is mono s16le at 14,493 Hz, with a ten-second host reservoir.

POST stays USB-only. Physical Windows/Linux Bluetooth and
installer/binary validation still need those platforms: local tests use fake
GATT/keyring peers. The CI workflow installs the BLE extra and collects Bleak
and keyring into each platform's binary; it has not been run/published here.

Dependencies use [Bleak's documented native pairing/GATT APIs](https://bleak.readthedocs.io/en/latest/api/client.html)
and [keyring's OS credential backends](https://github.com/jaraco/keyring).

## HTTPS security

The host performs direct HTTPS requests with the operating system's trusted CA
store. It does not use browser cookies, proxy credentials, shell commands, or
the local filesystem. URL credentials and non-HTTPS schemes are rejected. DNS
answers are checked before connecting; localhost, private, link-local,
multicast, reserved, and unspecified addresses are blocked. Every redirect is
resolved and checked again, and the TLS socket connects to the validated IP
while retaining the original hostname for SNI and certificate validation.

## Prebuilt binaries

The `host-binaries.yml` workflow builds the CLI plus a **onedir** Qt GUI on each
target OS. Keep the GUI's shared libraries and notices next to its executable;
do not copy only the executable. Existing release candidates are on the
[GitHub Releases](https://github.com/mete888/flipper_internet_bridge/releases) page.
This working-tree GUI change has not been published there.

Windows PowerShell:

```powershell
# Extract the GUI ZIP, then:
.\fib-bridge-desktop\fib-bridge-desktop.exe
# Optional terminal host:
.\fib-bridge-windows-x86_64.exe
```

Linux:

```sh
tar -xzf fib-bridge-linux-x86_64.tar.gz
cd fib-bridge-linux-x86_64-package
./install.sh
```

The Linux installer places the GUI, CLI and icon under the current user's XDG
data directory and adds a **Flipper Internet Bridge** graphical launcher to the
desktop application menu. It does not use `sudo`. Run `./uninstall.sh` from the
extracted package to remove the launcher. Replaced/removed GUI directories are
preserved in explicitly printed recovery directories; permission/keyring data
is not deleted. Both platforms use the existing gray Flipper icon.

Build packages on their **target OS**, with Python 3.12, a C compiler and the
GUI dependencies installed:

```sh
python -m pip install ".[desktop]" pyinstaller
# Linux: build the CLI first, then the GUI/package.
pyinstaller --onefile --collect-all bleak --collect-all keyring --name fib-bridge-linux-x86_64 host/fib_bridge_entry.py
python scripts/package_desktop_host.py --platform linux-x86_64
```

Windows PowerShell:

```powershell
python -m pip install ".[desktop]" pyinstaller
python scripts/package_desktop_host.py --platform windows-x86_64
```

Packages are generated in `dist/host-desktop/`. Packaging is not cross-compilation.
Linux desktop Bluetooth needs BlueZ and an unlocked secure user keyring; serial
access needs the normal user-group permissions. No privileged helper is used.

The binaries are currently unsigned. Windows SmartScreen or Linux desktop
policy may therefore ask for confirmation. Backend, real Qt widget and POSIX
virtual-serial tests run locally on macOS. The CI matrix is configured to run on
Windows/Linux, but these new native packages and physical Bluetooth/USB behavior
on those platforms have not yet been validated. Review dependency distribution
obligations before public binary publication.
