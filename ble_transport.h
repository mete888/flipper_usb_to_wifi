#pragma once

#include "usb_transport.h"

typedef struct BleTransport BleTransport;

/* The alpha reuses the byte-stream callbacks, not USB CDC or firmware RPC. */
BleTransport* ble_transport_alloc(
    UsbTransportReceiveCallback receive,
    UsbTransportEventCallback event,
    void* context);
bool ble_transport_start(BleTransport* transport);
void ble_transport_free(BleTransport* transport);
bool ble_transport_is_connected(BleTransport* transport);
void ble_transport_disconnect(BleTransport* transport);
bool ble_transport_send_until(
    BleTransport* transport, const uint8_t* data, size_t length, uint32_t deadline);
