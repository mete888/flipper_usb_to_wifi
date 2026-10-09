# Internet Bridge SDK — quick start

This source SDK lets another Flipper Zero FAP use an authorized desktop host as
an HTTPS transport. It is not a background service: Flipper OS runs one FAP at a
time, so each consumer compiles the client sources into its own application.
USB and Bluetooth are supported. The main Internet Bridge FAP does not need to
be open; the desktop **Flipper Internet Bridge** helper must be running.

## 1. Copy the SDK with one command

From this repository, replace `/path/to/your_fap` with your app directory:

```sh
python3 scripts/vendor_bridge_sdk.py \
  --destination /path/to/your_fap/vendor/internet_bridge
```

No Python packages are required. This copies only the SDK, required transport
and pairing sources/headers, and MIT license. Existing destinations and symlinks
are refused; export updates into a new directory and compare before replacing
your old vendor copy. The consumer manifest is never edited automatically.
On Windows, use `python` or `py -3` instead of `python3` if needed.

## 2. Add two source patterns

Keep your existing sources and append these in `application.fam`:

```python
sources=[
    "your_app.c",
    "vendor/internet_bridge/*.c",
    "vendor/internet_bridge/sdk/flipper/*.c",
]
```

The patterns include all mandatory pairing files; no hand-maintained file list
is needed. Use a stack of at least 4 KiB (`stack_size=4096` in the example).

## 3. Connect, GET, clean up

```c
#include "vendor/internet_bridge/sdk/flipper/fib_bridge_setup.h"

// on_chunk is your short body callback; app is your callback context.
FibBridgeClientCallbacks callbacks = {.on_body = on_chunk, .context = app};
FibBridgeClient* bridge = fib_bridge_client_alloc(NULL, &callbacks);
if(bridge && fib_bridge_client_connect_ui(bridge)) {
    bool sent = fib_bridge_client_get(bridge, "https://api.github.com/zen", 15000);
    // Check sent, run your event loop and call fib_bridge_client_tick(bridge).
    // Use fib_bridge_client_get_status() for HTTP status/errors/completion.
    (void)sent;
}
// After your event loop, at shutdown (not immediately after starting the GET):
// fib_bridge_client_free(bridge);
```

The ready-made screen handles USB/Bluetooth selection, explicit computer
approval, code display and waiting for separate desktop internet consent.
`connect_ui()` blocks only your application thread during setup, runs `tick()`
internally, returns true only when ready, and returns false on Back after
disconnecting. It refuses to interrupt an active request.

Call it before registering your fullscreen UI. For reconnection, disable your
fullscreen viewport, call setup, then re-enable it. Never call it from a GUI or
transport callback or while holding a mutex needed by your callbacks. After
setup, continue calling `fib_bridge_client_tick()` from your normal event loop
(for example every 100 ms).

Response chunks are delivered to `on_body`; they are not accumulated by the SDK.
Return `false` from that callback to stop a response when the consumer's own
limit is reached. `fib_bridge_client_cancel()` cancels only the request;
`fib_bridge_client_disconnect()` ends the connection/consent. Always call
`fib_bridge_client_free()` during shutdown to join workers and restore firmware
configuration. Never free while holding a mutex needed by your callbacks.

`examples/flipper_bridge_client/` is a complete standalone FAP using only the
public client API. Build it without modifying the main application manifest:

```sh
UFBT=/path/to/ufbt ./scripts/build_sdk_example.sh
```

The script exports into a temporary consumer directory and builds the same two
source patterns, then prints `fib_sdk_example.fap`. The example uses the setup
screen, performs a bounded HTTPS GET, pages through a 256-byte preview and
cancels/cleans up on Back. See
[`example_app.c`](../../examples/flipper_bridge_client/example_app.c) for the
complete implementation.

## Custom connection UI (optional)

Use `fib_bridge_client.h` if you want your own screens:

```c
fib_bridge_client_connect(bridge, FibBridgeTransportUSB);
// OR:
fib_bridge_client_connect(bridge, FibBridgeTransportBluetooth);
```

Poll `fib_bridge_client_get_pairing_status()`:

- `request_pending`: show `computer_name` and save the **displayed**
  `request_nonce`. Only after a user choice, call
  `fib_bridge_client_respond_to_computer(bridge, shown_nonce, true/false)`.
  Never auto-approve peers or pass a newly polled nonce for an old screen.
- `code_pending`: display the six-digit `code`; enter it in the desktop helper.
- Recognized peers skip the code, **not** desktop Allow Once/Deny internet consent.

`fib_bridge_client_known_computers()` returns pages of public computer IDs, up
to eight per page. `fib_bridge_client_revoke_computer()` revokes recognition
and disconnects that peer if current, without altering USB grants or system
Bluetooth bonds. These records are shared with Internet Bridge on the same
Flipper/microSD, so revocation affects its other SDK consumers too. Credential
tokens are never returned. Labels use the main FAP's `Bridge PC - XXXX` format.

## Constraints

- FIBP v1 supports one request at a time.
- Only HTTPS GET is exposed by this SDK version.
- The desktop helper must be running and the user must grant permission.
- USB temporarily owns CDC interface 1. Bluetooth owns the bridge GATT profile;
  both restore firmware configuration during shutdown. No automatic fallback.
- Existing `fib_bridge_client_start()` remains the USB shortcut. New consumers
  should use `status.connected`, `bluetooth_mode` and `bluetooth_connected`;
  legacy `usb_connected` retains its historical selected-channel meaning.
- One transport owner per running FAP. Internet Radio is not exposed by this SDK.
- Status callbacks may run on the worker or calling thread; body callbacks run
  on the transport worker. Keep them short, protect shared state and forward UI
  work to your event loop. Do not connect/disconnect/free or issue requests from
  callbacks. Pairing never grants internet consent; HTTPS/SSRF limits still apply.
- Builds and hardware-free tests do not certify physical BLE. See the SDK
  [validation report](../../docs/test-results-2026-10-08-sdk.md) for current
  evidence and hardware limits (available in the full source repository).
