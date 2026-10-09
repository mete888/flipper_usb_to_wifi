#include "ble_transport.h"
#include "ble_discovery_config.h"

#include <bt/bt_service/bt.h>
#include <furi.h>
#include <furi/core/memmgr_heap.h>
#include <furi_hal_bt.h>
#include <furi_hal_version.h>
#include <profiles/serial_profile.h>
#include <services/battery_service.h>
#include <services/serial_service.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BLE_RX_SIZE 2048U
#define BLE_ATT_CHUNK 20U /* Safe even at the minimum negotiated ATT MTU. */
#define BLE_FLAG_STOP (1UL << 0)
#define BLE_FLAG_RX (1UL << 1)
#define BLE_FLAG_STATE (1UL << 2)
#define BLE_FLAG_OVERFLOW (1UL << 3)

typedef struct {
    FuriHalBleProfileBase base;
    BleServiceSerial* serial;
    BleServiceBattery* battery;
} BridgeBleProfile;

struct BleTransport {
    UsbTransportReceiveCallback receive;
    UsbTransportEventCallback event;
    void* context;
    Bt* bt;
    BleServiceSerial* service;
    FuriThread* worker;
    FuriStreamBuffer* rx;
    FuriSemaphore* sent;
    FuriMutex* tx_mutex;
    volatile bool accepting;
    volatile bool ready;
    bool started;
    bool worker_started;
};

static const FuriHalBleProfileTemplate bridge_ble_template;

static void ble_signal(BleTransport* transport, uint32_t flag) {
    furi_thread_flags_set(furi_thread_get_id(transport->worker), flag);
}

/* GAP/service callbacks never run the parser or block on network/UI work. */
static uint16_t ble_serial_event(SerialServiceEvent event, void* context) {
    BleTransport* transport = context;
    if(!transport->accepting) return 0;
    if(event.event == SerialServiceEventTypeDataSent) {
        furi_semaphore_release(transport->sent);
    } else if(event.event == SerialServiceEventTypesBleResetRequest) {
        /* In this distinct profile, status=0 opens FIBP after the host has
         * subscribed to TX and paired. It MUST NOT reset the firmware RPC. */
        transport->ready = true;
        ble_signal(transport, BLE_FLAG_STATE);
    } else if(event.event == SerialServiceEventTypeDataReceived && transport->ready) {
        const size_t queued = furi_stream_buffer_send(
            transport->rx, event.data.buffer, event.data.size, 0);
        ble_signal(transport, queued == event.data.size ? BLE_FLAG_RX : BLE_FLAG_OVERFLOW);
    }
    return (uint16_t)furi_stream_buffer_spaces_available(transport->rx);
}

static FuriHalBleProfileBase* ble_profile_start(FuriHalBleProfileParams params) {
    BleTransport* transport = params;
    BridgeBleProfile* profile = malloc(sizeof(*profile));
    if(!profile) return NULL;
    profile->base.config = &bridge_ble_template;
    profile->battery = ble_svc_battery_start(true);
    profile->serial = ble_svc_serial_start();
    if(!profile->serial) {
        ble_svc_battery_stop(profile->battery);
        free(profile);
        return NULL;
    }
    transport->service = profile->serial;
    ble_svc_serial_set_callbacks(profile->serial, BLE_RX_SIZE, ble_serial_event, transport);
    transport->accepting = true;
    return &profile->base;
}

static void ble_profile_stop(FuriHalBleProfileBase* base) {
    BridgeBleProfile* profile = (BridgeBleProfile*)base;
    ble_svc_serial_stop(profile->serial);
    ble_svc_battery_stop(profile->battery);
    free(profile);
}

static void ble_profile_config(GapConfig* config, FuriHalBleProfileParams params) {
    UNUSED(params);
    ble_profile_serial->get_gap_config(config, NULL);
    /* Full bridge name + flags + TX power + 16-bit marker fit legacy ADV.
     * Neither the marker nor the name is authentication. */
    config->adv_service.UUID_Type = 1; /* SDK UUID_TYPE_16 */
    config->adv_service.Service_UUID_16 = FIB_BLE_DISCOVERY_UUID16;
    memset(config->adv_service.Service_UUID_128, 0, sizeof(config->adv_service.Service_UUID_128));
    /* Preserve AD type, but make the bridge's advertised name unambiguous. */
    const char* name = furi_hal_version_get_name_ptr();
    snprintf(config->adv_name + 1, sizeof(config->adv_name) - 1,
        "FZ Bridge %.6s", name ? name : "FIB");
    config->conn_param.conn_int_max = 0x0C; /* Request <=15 ms; central may differ. */
    config->bonding_mode = true;
    config->pairing_method = GapPairingPinCodeShow;
}

static const FuriHalBleProfileTemplate bridge_ble_template = {
    .start = ble_profile_start, .stop = ble_profile_stop, .get_gap_config = ble_profile_config};

static void ble_status_changed(BtStatus status, void* context) {
    BleTransport* transport = context;
    if(!transport->accepting) return;
    if(status != BtStatusConnected && transport->ready) {
        transport->ready = false;
        furi_semaphore_release(transport->sent);
        ble_signal(transport, BLE_FLAG_STATE);
    }
}

static int32_t ble_worker(void* context) {
    BleTransport* transport = context;
    bool reported_ready = false;
    uint8_t buffer[128];
    while(true) {
        const uint32_t flags = furi_thread_flags_wait(
            BLE_FLAG_STOP | BLE_FLAG_RX | BLE_FLAG_STATE | BLE_FLAG_OVERFLOW,
            FuriFlagWaitAny, FuriWaitForever);
        if(flags & FuriFlagError) continue;
        if(flags & BLE_FLAG_STOP) break;
        if(reported_ready != transport->ready) {
            reported_ready = transport->ready;
            if(!reported_ready) furi_stream_buffer_reset(transport->rx);
            transport->event(
                reported_ready ? UsbTransportEventUsbConnected : UsbTransportEventUsbDisconnected,
                transport->context);
            if(reported_ready) transport->event(UsbTransportEventPortOpened, transport->context);
        }
        if(flags & BLE_FLAG_OVERFLOW) {
            furi_stream_buffer_reset(transport->rx);
            transport->event(UsbTransportEventRxOverflow, transport->context);
        }
        if(flags & BLE_FLAG_RX) {
            size_t length;
            while((length = furi_stream_buffer_receive(transport->rx, buffer, sizeof(buffer), 0))) {
                if(transport->ready) transport->receive(buffer, length, transport->context);
            }
            if(transport->service && transport->accepting) {
                ble_svc_serial_notify_buffer_is_empty(transport->service);
            }
        }
    }
    return 0;
}

BleTransport* ble_transport_alloc(
    UsbTransportReceiveCallback receive, UsbTransportEventCallback event, void* context) {
    BleTransport* transport = calloc(1, sizeof(*transport));
    if(!transport) return NULL;
    transport->receive = receive;
    transport->event = event;
    transport->context = context;
    transport->rx = furi_stream_buffer_alloc(BLE_RX_SIZE, 1);
    transport->sent = furi_semaphore_alloc(1, 0);
    transport->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    transport->worker = furi_thread_alloc_ex("FibBleWorker", 2048, ble_worker, transport);
    if(!transport->rx || !transport->sent || !transport->tx_mutex || !transport->worker) {
        ble_transport_free(transport);
        return NULL;
    }
    return transport;
}

bool ble_transport_start(BleTransport* transport) {
    if(!transport || transport->started) return false;
    if(memmgr_get_free_heap() < 16U * 1024U ||
       memmgr_heap_get_max_free_block() < 4U * 1024U) return false;
    transport->bt = furi_record_open(RECORD_BT);
    furi_thread_start(transport->worker);
    transport->worker_started = true;
    bt_set_status_changed_callback(transport->bt, ble_status_changed, transport);
    if(!bt_profile_start(transport->bt, &bridge_ble_template, transport)) return false;
    transport->started = true;
    furi_hal_bt_start_advertising();
    return true;
}

bool ble_transport_is_connected(BleTransport* transport) {
    return transport && transport->accepting && transport->ready;
}

void ble_transport_disconnect(BleTransport* transport) {
    if(transport && transport->bt && transport->accepting) {
        /* Firmware bt_disconnect synchronously stops GAP advertising as well
         * as the link. Alpha still owns this profile: accept future requests. */
        bt_disconnect(transport->bt);
        if(transport->accepting) furi_hal_bt_start_advertising();
    }
}

bool ble_transport_send_until(
    BleTransport* transport, const uint8_t* data, size_t length, uint32_t deadline) {
    if(!ble_transport_is_connected(transport)) return false;
    const int32_t lock_ticks = (int32_t)(deadline - furi_get_tick());
    if(lock_ticks <= 0 || furi_mutex_acquire(transport->tx_mutex, lock_ticks) != FuriStatusOk) {
        return false;
    }
    bool success = true;
    for(size_t offset = 0; offset < length;) {
        const size_t chunk = MIN(BLE_ATT_CHUNK, length - offset);
        while(furi_semaphore_acquire(transport->sent, 0) == FuriStatusOk) {}
        if(!ble_transport_is_connected(transport) ||
           !ble_svc_serial_update_tx(transport->service, (uint8_t*)data + offset, chunk)) {
            success = false;
            break;
        }
        const int32_t ticks = (int32_t)(deadline - furi_get_tick());
        if(ticks <= 0 || furi_semaphore_acquire(transport->sent, ticks) != FuriStatusOk ||
           !ble_transport_is_connected(transport)) {
            success = false;
            break;
        }
        offset += chunk;
    }
    if(!success) {
        /* Do not reuse an uncertain indication/partial FIBP frame. */
        transport->ready = false;
        ble_signal(transport, BLE_FLAG_STATE);
    }
    furi_mutex_release(transport->tx_mutex);
    if(!success && transport->accepting) bt_disconnect(transport->bt);
    return success;
}

void ble_transport_free(BleTransport* transport) {
    if(!transport) return;
    transport->accepting = false;
    transport->ready = false;
    if(transport->sent) furi_semaphore_release(transport->sent);
    if(transport->bt) bt_set_status_changed_callback(transport->bt, NULL, NULL);
    if(transport->worker_started) {
        ble_signal(transport, BLE_FLAG_STOP);
        furi_thread_join(transport->worker);
    }
    if(transport->bt) {
        /* Restore even after a failed profile start; no bond deletion. */
        bt_profile_restore_default(transport->bt);
        furi_record_close(RECORD_BT);
    }
    if(transport->worker) furi_thread_free(transport->worker);
    if(transport->rx) furi_stream_buffer_free(transport->rx);
    if(transport->sent) furi_semaphore_free(transport->sent);
    if(transport->tx_mutex) furi_mutex_free(transport->tx_mutex);
    free(transport);
}
