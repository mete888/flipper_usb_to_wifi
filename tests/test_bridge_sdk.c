/* Public SDK + setup UI regression tests. Session/GUI doubles, not RF hardware. */
#include "sdk/flipper/fib_bridge_setup.h"
#include "bridge_session.h"
#include "ble_pairing_storage.h"
#include <furi.h>
#include <gui/gui.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BridgeSession { unsigned unused; };
static struct {
    BridgeSessionStatus status;
    BridgeSessionPairingStatus pairing;
    BridgeSessionUpdateCallback update;
    BridgeSessionBodyCallback body;
    void *context, *body_context;
    BridgeTransportMode mode;
    unsigned selects, disconnects, accepted, rejected, stale, freed, ticks, gets;
    bool fail_alloc, fail_start;
    char version[17];
} mock;

BridgeSession* bridge_session_alloc_with_version(BridgeSessionUpdateCallback cb, void* ctx, const char* version) {
    if(mock.fail_alloc) return NULL;
    mock.update = cb; mock.context = ctx;
    snprintf(mock.version, sizeof(mock.version), "%s", version ? version : "default");
    return calloc(1U, sizeof(BridgeSession));
}
void bridge_session_free(BridgeSession* s) { ++mock.freed; free(s); }
void bridge_session_set_body_callback(BridgeSession* s, BridgeSessionBodyCallback cb, void* ctx) {
    (void)s; mock.body = cb; mock.body_context = ctx;
}
bool bridge_session_select_transport(BridgeSession* s, BridgeTransportMode mode) {
    (void)s; ++mock.selects; mock.mode = mode;
    memset(&mock.status, 0, sizeof(mock.status)); memset(&mock.pairing, 0, sizeof(mock.pairing));
    if(mode == BridgeTransportNone) { ++mock.disconnects; return true; }
    mock.status.bluetooth_alpha = mode == BridgeTransportBluetooth;
    if(mock.fail_start) { mock.status.state = BridgeSessionStateError; return false; }
    mock.status.usb_connected = true;
    mock.status.state = BridgeSessionStateWaitingForHelper;
    strcpy(mock.status.detail, "Waiting for helper");
    if(mode == BridgeTransportBluetooth) {
        mock.pairing.request_pending = true; mock.pairing.request_nonce = 77U;
        mock.pairing.host_id[0] = 0xAB; strcpy(mock.pairing.host_label, "Bridge PC - AB00");
    }
    return true;
}
void bridge_session_tick(BridgeSession* s) { (void)s; ++mock.ticks; }
bool bridge_session_ping(BridgeSession* s) { (void)s; return mock.status.helper_present; }
bool bridge_session_request_get(BridgeSession* s, const char* url, uint32_t timeout) {
    (void)s; (void)timeout;
    if(!url || strncmp(url, "https://", 8U) || mock.status.active_request ||
       (mock.status.permission != BridgePermissionAllowedOnce && mock.status.permission != BridgePermissionAllowedAlways)) return false;
    ++mock.gets; mock.status.active_request = true; return true;
}
bool bridge_session_cancel(BridgeSession* s) { (void)s; bool was = mock.status.active_request; mock.status.active_request = false; return was; }
bool bridge_session_has_active_request(BridgeSession* s) { (void)s; return mock.status.active_request; }
void bridge_session_get_status(BridgeSession* s, BridgeSessionStatus* status) { (void)s; *status = mock.status; }
void bridge_session_get_pairing_status(BridgeSession* s, BridgeSessionPairingStatus* status) { (void)s; *status = mock.pairing; }
bool bridge_session_respond_bluetooth_request(BridgeSession* s, uint64_t nonce, bool allow) {
    (void)s;
    if(!mock.pairing.request_pending || nonce != mock.pairing.request_nonce) { ++mock.stale; return false; }
    mock.pairing.request_pending = false;
    if(!allow) { ++mock.rejected; mock.status.usb_connected = false; return false; }
    ++mock.accepted;
    mock.pairing.code_pending = !mock.pairing.known_host;
    if(mock.pairing.code_pending) strcpy(mock.pairing.code, "123456");
    mock.status.state = BridgeSessionStatePermissionPending;
    mock.status.permission = BridgePermissionPending;
    strcpy(mock.status.detail, "Awaiting desktop permission");
    return true;
}
bool bridge_session_revoke_bluetooth_host(BridgeSession* s, const uint8_t host[16]) {
    (void)s; return host[0] == 0xAB;
}
size_t fib_pair_storage_list(uint8_t (*hosts)[16], size_t capacity, size_t offset, bool* more) {
    if(offset) return 0U;
    assert(capacity <= 8U); memset(hosts[0], 0, 16U); hosts[0][0] = 0xAB;
    if(more) *more = false;
    return 1U;
}
static void grant(void) {
    mock.status.state = BridgeSessionStateReady;
    mock.status.permission = BridgePermissionAllowedOnce;
    mock.status.usb_connected = mock.status.helper_present = true;
    mock.status.selected_major = 1U;
    memset(&mock.pairing, 0, sizeof(mock.pairing));
}

struct FuriMutex { bool locked; };
struct FuriMessageQueue { size_t size; unsigned count; unsigned char value[64]; };
struct ViewPort {
    void (*draw)(Canvas*, void*); void (*input)(InputEvent*, void*);
    void *draw_context, *input_context; bool attached;
};
struct Gui { unsigned unused; };
static struct Gui gui;
static ViewPort* active_view;
static Canvas last_canvas;
static unsigned mutexes, queues, viewports, records, step;
static enum { ScenarioUSB, ScenarioBLE, ScenarioKnown, ScenarioStale, ScenarioDeny,
              ScenarioCancelCode, ScenarioRetry, ScenarioBack, ScenarioFailStart } scenario;
FuriMutex* furi_mutex_alloc(FuriMutexType type) { (void)type; ++mutexes; return calloc(1U, sizeof(FuriMutex)); }
void furi_mutex_free(FuriMutex* m) { assert(!m->locked); --mutexes; free(m); }
FuriStatus furi_mutex_acquire(FuriMutex* m, uint32_t timeout) { (void)timeout; assert(!m->locked); m->locked = true; return FuriStatusOk; }
FuriStatus furi_mutex_release(FuriMutex* m) { assert(m->locked); m->locked = false; return FuriStatusOk; }
FuriMessageQueue* furi_message_queue_alloc(uint32_t count, uint32_t size) {
    (void)count; assert(size <= 64U); ++queues;
    FuriMessageQueue* q = calloc(1U, sizeof(*q)); q->size = size; return q;
}
void furi_message_queue_free(FuriMessageQueue* q) { --queues; free(q); }
FuriStatus furi_message_queue_put(FuriMessageQueue* q, const void* value, uint32_t timeout) {
    (void)timeout; assert(!q->count); memcpy(q->value, value, q->size); q->count = 1U; return FuriStatusOk;
}
static void send_key(InputKey key) {
    InputEvent event = {.key = key, .type = InputTypeShort};
    active_view->input(&event, active_view->input_context);
}
FuriStatus furi_message_queue_get(FuriMessageQueue* q, void* value, uint32_t timeout) {
    (void)timeout; assert(active_view && step < 10U);
    unsigned current = step++;
    if(scenario == ScenarioBack) send_key(InputKeyBack);
    else if(scenario == ScenarioUSB || scenario == ScenarioRetry || scenario == ScenarioFailStart) {
        if(current == 0U) send_key(InputKeyOk);
        else if(scenario == ScenarioFailStart) { assert(mock.status.state == BridgeSessionStateError); send_key(InputKeyBack); }
        else if(scenario == ScenarioRetry && current == 1U) {
            mock.status.permission = BridgePermissionDenied; mock.status.state = BridgeSessionStatePermissionDenied;
        } else if(scenario == ScenarioRetry && current == 2U) send_key(InputKeyOk);
        else grant();
    } else {
        if(current == 0U) send_key(InputKeyDown);
        else if(current == 1U) { send_key(InputKeyOk); }
        else if(current == 2U) {
            assert(strstr(last_canvas.text, "Bridge PC - AB00"));
            if(scenario == ScenarioKnown) mock.pairing.known_host = true;
            if(scenario == ScenarioStale) ++mock.pairing.request_nonce;
            send_key(scenario == ScenarioDeny ? InputKeyLeft : InputKeyRight);
        } else {
            if(scenario == ScenarioStale) { assert(mock.stale == 1U && !mock.accepted); send_key(InputKeyBack); }
            else if(scenario == ScenarioDeny) { assert(mock.rejected == 1U && !mock.accepted); send_key(InputKeyBack); }
            else {
                assert(mock.accepted == 1U && mock.status.permission == BridgePermissionPending);
                if(scenario != ScenarioKnown) assert(strstr(last_canvas.text, "123456"));
                else assert(!strstr(last_canvas.text, "123456"));
                if(scenario == ScenarioCancelCode) send_key(InputKeyBack);
                else grant();
            }
        }
    }
    if(!q->count) return FuriStatusError;
    memcpy(value, q->value, q->size); q->count = 0U; return FuriStatusOk;
}
uint32_t furi_ms_to_ticks(uint32_t ms) { return ms; }
void* furi_record_open(const char* name) { assert(!strcmp(name, RECORD_GUI)); ++records; return &gui; }
void furi_record_close(const char* name) { assert(!strcmp(name, RECORD_GUI)); --records; }
void canvas_clear(Canvas* c) { memset(c, 0, sizeof(*c)); }
void canvas_set_font(Canvas* c, Font font) { c->font = font; }
uint16_t canvas_string_width(Canvas* c, const char* text) { (void)c; return (uint16_t)(strlen(text) * 5U); }
void canvas_draw_str(Canvas* c, int32_t x, int32_t y, const char* text) {
    assert(x >= 0 && y >= 0 && y < 64 && x + canvas_string_width(c, text) <= 128);
    size_t used = strlen(c->text); assert(used + strlen(text) + 2U < sizeof(c->text));
    snprintf(c->text + used, sizeof(c->text) - used, "%s\n", text);
}
void canvas_draw_rframe(Canvas* c, int32_t x, int32_t y, size_t w, size_t h, size_t r) {
    (void)c; (void)r; assert(x >= 0 && y >= 0 && (size_t)x + w <= 128U && (size_t)y + h <= 64U);
}
ViewPort* view_port_alloc(void) { ++viewports; return calloc(1U, sizeof(ViewPort)); }
void view_port_free(ViewPort* v) { assert(!v->attached); --viewports; free(v); }
void view_port_draw_callback_set(ViewPort* v, void (*cb)(Canvas*, void*), void* ctx) { v->draw = cb; v->draw_context = ctx; }
void view_port_input_callback_set(ViewPort* v, void (*cb)(InputEvent*, void*), void* ctx) { v->input = cb; v->input_context = ctx; }
void view_port_enabled_set(ViewPort* v, bool enabled) { (void)v; (void)enabled; }
void view_port_update(ViewPort* v) { assert(v->attached); v->draw(&last_canvas, v->draw_context); }
void gui_add_view_port(Gui* g, ViewPort* v, GuiLayer layer) { (void)g; (void)layer; v->attached = true; active_view = v; }
void gui_remove_view_port(Gui* g, ViewPort* v) { (void)g; v->attached = false; active_view = NULL; }

static unsigned status_calls, body_calls;
static void on_status(void* ctx, const FibBridgeStatus* status) { assert(ctx == &status_calls); ++status_calls; assert(status->connected == status->usb_connected); }
static bool on_body(void* ctx, const uint8_t* data, size_t size) { (void)ctx; assert(size == 3U && !memcmp(data, "abc", 3U)); ++body_calls; return false; }
int main(void) {
    assert(!fib_bridge_client_start(NULL) && !fib_bridge_client_connect(NULL, FibBridgeTransportBluetooth));
    assert(!fib_bridge_client_connect_ui(NULL) && !fib_bridge_client_disconnect(NULL));
    mock.fail_alloc = true; assert(!fib_bridge_client_alloc(NULL, NULL)); mock.fail_alloc = false;
    FibBridgeClientConfig config = {.app_version = "consumer-1"};
    FibBridgeClientCallbacks callbacks = {.on_status = on_status, .on_body = on_body, .context = &status_calls};
    FibBridgeClient* client = fib_bridge_client_alloc(&config, &callbacks); assert(client);
    assert(!strcmp(mock.version, "consumer-1"));
    assert(!fib_bridge_client_connect(client, (FibBridgeTransport)999));
    assert(fib_bridge_client_start(client) && mock.mode == BridgeTransportUSB);
    assert(!fib_bridge_client_get(client, "https://example.com", 0U));
    grant(); assert(fib_bridge_client_is_ready(client));
    mock.update(mock.context); assert(status_calls == 1U);
    assert(!mock.body(mock.body_context, (const uint8_t*)"abc", 3U) && body_calls == 1U);
    assert(fib_bridge_client_get(client, "https://example.com", 0U));
    assert(!fib_bridge_client_connect_ui(client));
    assert(fib_bridge_client_cancel(client));
    assert(fib_bridge_client_connect(client, FibBridgeTransportBluetooth));
    FibBridgeStatus status; fib_bridge_client_get_status(client, &status);
    assert(status.bluetooth_mode && status.bluetooth_connected && status.connected);
    FibBridgePairingStatus pairing; fib_bridge_client_get_pairing_status(client, &pairing);
    assert(pairing.request_pending && pairing.request_nonce == 77U && !pairing.code_pending);
    assert(!fib_bridge_client_respond_to_computer(client, 76U, true));
    assert(fib_bridge_client_respond_to_computer(client, 77U, true));
    fib_bridge_client_get_pairing_status(client, &pairing); assert(pairing.code_pending && !strcmp(pairing.code, "123456"));
    assert(!fib_bridge_client_is_ready(client)); /* Pairing is not internet consent. */
    uint8_t ids[8][16]; bool more = true;
    assert(fib_bridge_client_known_computers(client, ids, 8U, 0U, &more) == 1U && !more);
    assert(fib_bridge_client_revoke_computer(client, ids[0]));
    assert(!fib_bridge_client_known_computers(client, ids, 9U, 0U, &more));
    fib_bridge_client_get_pairing_status(NULL, &pairing); assert(!pairing.request_pending && !pairing.code[0]);
    fib_bridge_client_free(client); assert(mock.freed == 1U);

    for(int which = ScenarioUSB; which <= ScenarioFailStart; ++which) {
        memset(&mock, 0, sizeof(mock)); step = 0U; scenario = which;
        mock.fail_start = scenario == ScenarioFailStart;
        client = fib_bridge_client_alloc(NULL, NULL); assert(client);
        bool result = fib_bridge_client_connect_ui(client);
        bool expected = scenario == ScenarioUSB || scenario == ScenarioBLE || scenario == ScenarioKnown || scenario == ScenarioRetry;
        assert(result == expected);
        if(!result) assert(mock.mode == BridgeTransportNone);
        assert(!mutexes && !queues && !records && !viewports && !active_view);
        fib_bridge_client_free(client);
    }
    puts("SDK USB/BLE, nonce/code/consent, modal cleanup and public callbacks: PASS (doubles)");
    return 0;
}
