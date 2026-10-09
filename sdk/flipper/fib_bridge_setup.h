#pragma once

#include "fib_bridge_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ready-made modal connection UI. Call on the application thread, before
 * registering your own fullscreen UI or after disabling it. This runs tick()
 * while the user chooses USB/Bluetooth, approves a displayed computer and enters
 * its code on the desktop. It never grants desktop internet consent itself.
 * Returns true only when ready; Back disconnects and returns false.
 * Active requests are rejected, not cancelled by surprise. Do not call from a
 * callback, GUI/input worker or while holding a mutex needed by your callbacks. */
bool fib_bridge_client_connect_ui(FibBridgeClient* client);

#ifdef __cplusplus
}
#endif
