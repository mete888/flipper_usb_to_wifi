#include "fib_bridge_setup.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    InputEvent input;
    uint64_t shown_nonce;
} SetupEvent;

typedef struct {
    FibBridgeClient* client;
    FuriMutex* mutex;
    FuriMessageQueue* events;
    FibBridgeStatus status;
    FibBridgePairingStatus pairing;
    FibBridgeTransport selection;
    bool choosing;
    uint64_t shown_nonce;
} Setup;

static void fit(Canvas* canvas, int x, int y, const char* text) {
    char line[129];
    snprintf(line, sizeof(line), "%s", text);
    while(line[0] && canvas_string_width(canvas, line) > 120U) line[strlen(line) - 1U] = '\0';
    canvas_draw_str(canvas, x, y, line);
}

static void details(Canvas* canvas, const char* text) {
    for(unsigned row = 0U; row < 2U && *text; ++row) {
        char line[96];
        size_t used = 0U, space = 0U;
        while(text[used] && text[used] != '\n' && used < sizeof(line) - 1U) {
            line[used] = text[used]; line[used + 1U] = '\0';
            if(canvas_string_width(canvas, line) > 120U) break;
            if(text[used] == ' ') space = used;
            ++used;
        }
        if(!used && *text != '\n') used = 1U;
        else if(text[used] && text[used] != '\n' && space) used = space;
        memcpy(line, text, used); line[used] = '\0';
        canvas_draw_str(canvas, 3, 37 + (int)(row * 11U), line);
        text += used;
        if(*text == '\n') ++text;
        else while(*text == ' ') ++text;
    }
}

static void draw(Canvas* canvas, void* context) {
    Setup* setup = context;
    furi_mutex_acquire(setup->mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    fit(canvas, 3, 11, "Internet Bridge");
    canvas_set_font(canvas, FontSecondary);
    setup->shown_nonce = 0U;
    if(setup->choosing) {
        int y = setup->selection == FibBridgeTransportUSB ? 17 : 34;
        canvas_draw_rframe(canvas, 2, y, 124U, 16U, 3U);
        fit(canvas, 8, 28, "USB");
        fit(canvas, 8, 45, "Bluetooth");
        fit(canvas, 3, 62, "OK: connect   BACK: exit");
    } else if(setup->pairing.request_pending) {
        fit(canvas, 3, 25, setup->pairing.computer_name);
        fit(canvas, 3, 39, setup->pairing.known_computer ? "Recognized computer" : "New computer");
        fit(canvas, 3, 62, "< Deny        Connect >");
        /* Capture the exact peer displayed, not the peer present at key handling. */
        setup->shown_nonce = setup->pairing.request_nonce;
    } else if(setup->pairing.code_pending) {
        canvas_set_font(canvas, FontPrimary);
        fit(canvas, 40, 29, setup->pairing.code);
        canvas_set_font(canvas, FontSecondary);
        fit(canvas, 3, 44, "Enter code on computer");
        fit(canvas, 3, 62, "BACK: cancel pairing");
    } else {
        fit(canvas, 3, 25, setup->selection == FibBridgeTransportBluetooth ? "Bluetooth connection" : "USB connection");
        details(canvas, setup->status.detail);
        fit(canvas, 3, 62, "OK: retry   BACK: cancel");
    }
    furi_mutex_release(setup->mutex);
}

static void input(InputEvent* event, void* context) {
    Setup* setup = context;
    if(event->type != InputTypeShort) return;
    SetupEvent queued = {.input = *event};
    furi_mutex_acquire(setup->mutex, FuriWaitForever);
    queued.shown_nonce = setup->shown_nonce;
    furi_mutex_release(setup->mutex);
    furi_message_queue_put(setup->events, &queued, 0);
}

bool fib_bridge_client_connect_ui(FibBridgeClient* client) {
    if(!client || fib_bridge_client_has_active_request(client)) return false;
    if(fib_bridge_client_is_ready(client)) return true;
    Setup* setup = calloc(1U, sizeof(*setup));
    if(!setup) return false;
    setup->client = client;
    setup->selection = FibBridgeTransportUSB;
    setup->choosing = true;
    setup->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    setup->events = furi_message_queue_alloc(8U, sizeof(SetupEvent));
    if(!setup->mutex || !setup->events) {
        if(setup->events) furi_message_queue_free(setup->events);
        if(setup->mutex) furi_mutex_free(setup->mutex);
        free(setup);
        return false;
    }
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, draw, setup);
    view_port_input_callback_set(viewport, input, setup);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);
    bool ready = false;
    bool running = true;
    while(running) {
        fib_bridge_client_tick(client);
        FibBridgeStatus status;
        FibBridgePairingStatus pairing;
        fib_bridge_client_get_status(client, &status);
        fib_bridge_client_get_pairing_status(client, &pairing);
        furi_mutex_acquire(setup->mutex, FuriWaitForever);
        setup->status = status;
        setup->pairing = pairing;
        bool choosing = setup->choosing;
        FibBridgeTransport selection = setup->selection;
        furi_mutex_release(setup->mutex);
        if(!choosing && fib_bridge_client_is_ready(client)) { ready = true; break; }
        view_port_update(viewport);
        SetupEvent event;
        if(furi_message_queue_get(setup->events, &event, furi_ms_to_ticks(100U)) != FuriStatusOk) continue;
        if(event.input.key == InputKeyBack) running = false;
        else if(choosing) {
            if(event.input.key == InputKeyUp || event.input.key == InputKeyDown) {
                furi_mutex_acquire(setup->mutex, FuriWaitForever);
                setup->selection = selection == FibBridgeTransportUSB ? FibBridgeTransportBluetooth : FibBridgeTransportUSB;
                furi_mutex_release(setup->mutex);
            } else if(event.input.key == InputKeyOk) {
                furi_mutex_acquire(setup->mutex, FuriWaitForever);
                setup->choosing = false;
                furi_mutex_release(setup->mutex);
                fib_bridge_client_disconnect(client);
                fib_bridge_client_connect(client, selection);
            }
        } else if(event.shown_nonce) {
            if(event.input.key == InputKeyRight || event.input.key == InputKeyLeft)
                fib_bridge_client_respond_to_computer(client, event.shown_nonce, event.input.key == InputKeyRight);
        } else if(event.input.key == InputKeyOk && !pairing.request_pending && !pairing.code_pending) {
            fib_bridge_client_disconnect(client);
            fib_bridge_client_connect(client, selection);
        }
    }
    /* Removing the viewport drains GUI owners before freeing their context. */
    view_port_enabled_set(viewport, false);
    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(setup->events);
    furi_mutex_free(setup->mutex);
    free(setup);
    if(!ready) fib_bridge_client_disconnect(client);
    return ready;
}
