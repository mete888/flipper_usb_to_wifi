#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FibBridgeClient FibBridgeClient;

#define FIB_BRIDGE_KNOWN_COMPUTER_PAGE_SIZE 8U

typedef enum {
    FibBridgeTransportNone,
    FibBridgeTransportUSB,
    FibBridgeTransportBluetooth,
} FibBridgeTransport;

typedef enum {
    FibBridgeStateDisconnected,
    FibBridgeStateWaitingForHost,
    FibBridgeStateHandshaking,
    FibBridgeStateHostNotFound,
    FibBridgeStatePermissionPending,
    FibBridgeStatePermissionDenied,
    FibBridgeStateReady,
    FibBridgeStatePinging,
    FibBridgeStateSendingRequest,
    FibBridgeStateReceivingResponse,
    FibBridgeStateComplete,
    FibBridgeStateCancelled,
    FibBridgeStateTimedOut,
    FibBridgeStateError,
} FibBridgeState;

typedef enum {
    FibBridgePermissionUnknown,
    FibBridgePermissionPending,
    FibBridgePermissionDenied,
    FibBridgePermissionAllowedOnce,
    FibBridgePermissionAllowedAlways,
} FibBridgePermission;

typedef struct {
    FibBridgeState state;
    FibBridgePermission permission;
    bool usb_connected;
    bool host_present;
    bool active_request;
    bool response_truncated;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint16_t http_status;
    uint32_t request_id;
    uint32_t response_bytes;
    uint32_t declared_response_bytes;
    char detail[129];
    /* usb_connected retains its legacy meaning (selected transport connected).
     * Prefer these unambiguous fields in new consumers. */
    bool connected;
    bool bluetooth_mode;
    bool bluetooth_connected;
} FibBridgeStatus;

typedef struct {
    bool request_pending;
    bool known_computer;
    bool code_pending;
    uint64_t request_nonce;
    uint8_t computer_id[16];
    char computer_name[24];
    char code[7];
} FibBridgePairingStatus;

typedef struct {
    /* Sent in HELLO. UTF-8, 1...16 bytes. Defaults to the bridge version. */
    const char* app_version;
} FibBridgeClientConfig;

/* Callbacks may run on the transport worker or the calling thread. Keep them
 * short; do not connect/disconnect/free or issue requests from inside callbacks.
 * Forward status to your UI/event loop instead. Returning false from
 * on_body aborts the active response. */
typedef void (*FibBridgeStatusCallback)(void* context, const FibBridgeStatus* status);
typedef bool (*FibBridgeBodyCallback)(void* context, const uint8_t* data, size_t length);

typedef struct {
    FibBridgeStatusCallback on_status;
    FibBridgeBodyCallback on_body;
    void* context;
} FibBridgeClientCallbacks;

FibBridgeClient* fib_bridge_client_alloc(
    const FibBridgeClientConfig* config,
    const FibBridgeClientCallbacks* callbacks);
void fib_bridge_client_free(FibBridgeClient* client);

bool fib_bridge_client_start(FibBridgeClient* client);
/* App/event-loop thread only. start() remains the legacy USB shortcut.
 * Switching transports ends the old request and permission session. No fallback.
 * Bluetooth requires explicit computer approval and independent desktop consent. */
bool fib_bridge_client_connect(FibBridgeClient* client, FibBridgeTransport transport);
bool fib_bridge_client_disconnect(FibBridgeClient* client);
void fib_bridge_client_get_pairing_status(FibBridgeClient*, FibBridgePairingStatus*);
/* Pass the nonce shown with the computer name, never a newly polled nonce. */
bool fib_bridge_client_respond_to_computer(FibBridgeClient*, uint64_t nonce, bool allow);
/* App thread only; bounded pages of public IDs, never credential tokens.
 * Recognition is shared with Internet Bridge on this Flipper's microSD card. */
size_t fib_bridge_client_known_computers(
    FibBridgeClient*, uint8_t (*ids)[16], size_t capacity, size_t offset, bool* more);
bool fib_bridge_client_revoke_computer(FibBridgeClient*, const uint8_t id[16]);
void fib_bridge_client_tick(FibBridgeClient* client);
bool fib_bridge_client_ping(FibBridgeClient* client);

/* FIBP v1 supports one active HTTPS GET at a time. A zero timeout selects the
 * default; larger values are clamped to the protocol maximum. */
bool fib_bridge_client_get(FibBridgeClient* client, const char* url, uint32_t timeout_ms);
bool fib_bridge_client_cancel(FibBridgeClient* client);
bool fib_bridge_client_is_ready(FibBridgeClient* client);
bool fib_bridge_client_has_active_request(FibBridgeClient* client);
void fib_bridge_client_get_status(FibBridgeClient* client, FibBridgeStatus* status);

#ifdef __cplusplus
}
#endif
