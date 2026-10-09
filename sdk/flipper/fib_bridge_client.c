#include "fib_bridge_client.h"

#include "../../bridge_session.h"
#include "../../ble_pairing_storage.h"

#include <stdlib.h>
#include <string.h>

struct FibBridgeClient {
    BridgeSession* session;
    FibBridgeClientCallbacks callbacks;
};

static FibBridgeState fib_bridge_map_state(BridgeSessionState state) {
    switch(state) {
    case BridgeSessionStateDisconnected:
        return FibBridgeStateDisconnected;
    case BridgeSessionStateWaitingForHelper:
        return FibBridgeStateWaitingForHost;
    case BridgeSessionStateWaitingForHelloAck:
        return FibBridgeStateHandshaking;
    case BridgeSessionStateHelperNotFound:
        return FibBridgeStateHostNotFound;
    case BridgeSessionStatePermissionPending:
        return FibBridgeStatePermissionPending;
    case BridgeSessionStatePermissionDenied:
        return FibBridgeStatePermissionDenied;
    case BridgeSessionStateReady:
        return FibBridgeStateReady;
    case BridgeSessionStatePinging:
        return FibBridgeStatePinging;
    case BridgeSessionStateSendingRequest:
        return FibBridgeStateSendingRequest;
    case BridgeSessionStateReceivingResponse:
        return FibBridgeStateReceivingResponse;
    case BridgeSessionStateComplete:
        return FibBridgeStateComplete;
    case BridgeSessionStateCancelled:
        return FibBridgeStateCancelled;
    case BridgeSessionStateTimedOut:
        return FibBridgeStateTimedOut;
    case BridgeSessionStateError:
    default:
        return FibBridgeStateError;
    }
}

static void fib_bridge_copy_status(
    const BridgeSessionStatus* source,
    FibBridgeStatus* destination) {
    memset(destination, 0, sizeof(*destination));
    destination->state = fib_bridge_map_state(source->state);
    destination->permission = (FibBridgePermission)source->permission;
    destination->usb_connected = source->usb_connected;
    destination->connected = source->usb_connected;
    destination->bluetooth_mode = source->bluetooth_alpha;
    destination->bluetooth_connected = source->bluetooth_alpha && source->usb_connected;
    destination->host_present = source->helper_present;
    destination->active_request = source->active_request;
    destination->response_truncated = source->response_truncated;
    destination->protocol_major = source->selected_major;
    destination->protocol_minor = source->selected_minor;
    destination->http_status = source->http_status;
    destination->request_id = source->active_request_id;
    destination->response_bytes = source->response_bytes;
    destination->declared_response_bytes = source->declared_response_bytes;
    memcpy(destination->detail, source->detail, sizeof(destination->detail));
    destination->detail[sizeof(destination->detail) - 1U] = '\0';
}

static void fib_bridge_on_session_update(void* context) {
    FibBridgeClient* client = context;
    if(!client || !client->callbacks.on_status) return;
    BridgeSessionStatus source;
    FibBridgeStatus status;
    bridge_session_get_status(client->session, &source);
    fib_bridge_copy_status(&source, &status);
    client->callbacks.on_status(client->callbacks.context, &status);
}

static bool fib_bridge_on_body(void* context, const uint8_t* data, size_t length) {
    FibBridgeClient* client = context;
    if(!client || !client->callbacks.on_body) return true;
    return client->callbacks.on_body(client->callbacks.context, data, length);
}

FibBridgeClient* fib_bridge_client_alloc(
    const FibBridgeClientConfig* config,
    const FibBridgeClientCallbacks* callbacks) {
    FibBridgeClient* client = malloc(sizeof(FibBridgeClient));
    if(!client) return NULL;
    memset(client, 0, sizeof(*client));
    if(callbacks) client->callbacks = *callbacks;

    const char* version = config ? config->app_version : NULL;
    client->session =
        bridge_session_alloc_with_version(fib_bridge_on_session_update, client, version);
    if(!client->session) {
        free(client);
        return NULL;
    }
    bridge_session_set_body_callback(client->session, fib_bridge_on_body, client);
    return client;
}

void fib_bridge_client_free(FibBridgeClient* client) {
    if(!client) return;
    if(client->session) {
        bridge_session_set_body_callback(client->session, NULL, NULL);
        bridge_session_free(client->session);
    }
    memset(client, 0, sizeof(*client));
    free(client);
}

bool fib_bridge_client_start(FibBridgeClient* client) {
    return fib_bridge_client_connect(client, FibBridgeTransportUSB);
}

bool fib_bridge_client_connect(FibBridgeClient* client, FibBridgeTransport transport) {
    if(!client) return false;
    switch(transport) {
    case FibBridgeTransportNone:
        return bridge_session_select_transport(client->session, BridgeTransportNone);
    case FibBridgeTransportUSB:
        return bridge_session_select_transport(client->session, BridgeTransportUSB);
    case FibBridgeTransportBluetooth:
        return bridge_session_select_transport(client->session, BridgeTransportBluetooth);
    default:
        return false;
    }
}

bool fib_bridge_client_disconnect(FibBridgeClient* client) {
    return fib_bridge_client_connect(client, FibBridgeTransportNone);
}

void fib_bridge_client_get_pairing_status(FibBridgeClient* client, FibBridgePairingStatus* status) {
    if(!status) return;
    memset(status, 0, sizeof(*status));
    if(!client) return;
    BridgeSessionPairingStatus source;
    bridge_session_get_pairing_status(client->session, &source);
    status->request_pending = source.request_pending;
    status->known_computer = source.known_host;
    status->code_pending = source.code_pending;
    status->request_nonce = source.request_nonce;
    memcpy(status->computer_id, source.host_id, sizeof(status->computer_id));
    memcpy(status->computer_name, source.host_label, sizeof(status->computer_name));
    status->computer_name[sizeof(status->computer_name) - 1U] = '\0';
    memcpy(status->code, source.code, sizeof(status->code));
    status->code[sizeof(status->code) - 1U] = '\0';
}

bool fib_bridge_client_respond_to_computer(FibBridgeClient* client, uint64_t nonce, bool allow) {
    return client && bridge_session_respond_bluetooth_request(client->session, nonce, allow);
}

size_t fib_bridge_client_known_computers(
    FibBridgeClient* client, uint8_t (*ids)[16], size_t capacity, size_t offset, bool* more) {
    if(more) *more = false;
    if(!client || !ids || !capacity || capacity > FIB_BRIDGE_KNOWN_COMPUTER_PAGE_SIZE) return 0U;
    return fib_pair_storage_list(ids, capacity, offset, more);
}

bool fib_bridge_client_revoke_computer(FibBridgeClient* client, const uint8_t id[16]) {
    return client && id && bridge_session_revoke_bluetooth_host(client->session, id);
}

void fib_bridge_client_tick(FibBridgeClient* client) {
    if(client) bridge_session_tick(client->session);
}

bool fib_bridge_client_ping(FibBridgeClient* client) {
    return client && bridge_session_ping(client->session);
}

bool fib_bridge_client_get(FibBridgeClient* client, const char* url, uint32_t timeout_ms) {
    return client && bridge_session_request_get(client->session, url, timeout_ms);
}

bool fib_bridge_client_cancel(FibBridgeClient* client) {
    return client && bridge_session_cancel(client->session);
}

bool fib_bridge_client_is_ready(FibBridgeClient* client) {
    if(!client) return false;
    BridgeSessionStatus snapshot;
    bridge_session_get_status(client->session, &snapshot);
    return (snapshot.state == BridgeSessionStateReady || snapshot.state == BridgeSessionStateComplete) &&
        snapshot.usb_connected && snapshot.helper_present && snapshot.selected_major != 0U &&
        (snapshot.permission == BridgePermissionAllowedOnce ||
         snapshot.permission == BridgePermissionAllowedAlways);
}

bool fib_bridge_client_has_active_request(FibBridgeClient* client) {
    return client && bridge_session_has_active_request(client->session);
}

void fib_bridge_client_get_status(FibBridgeClient* client, FibBridgeStatus* status) {
    if(!status) return;
    memset(status, 0, sizeof(*status));
    if(!client) {
        status->state = FibBridgeStateError;
        return;
    }
    BridgeSessionStatus source;
    bridge_session_get_status(client->session, &source);
    fib_bridge_copy_status(&source, status);
}
