#pragma once
#include "bridge_session.h"

typedef enum {
    FibMenuToolbox,
    FibMenuTestConnection,
    FibMenuDownloadSample,
    FibMenuGetDateTime,
    FibMenuCustomUrl,
    FibMenuConnectionInfo,
    FibMenuBluetoothRequests,
    FibMenuBluetoothPairings,
} FibMenuItem;

typedef struct {
    const char* label;
    FibMenuItem action;
} BridgeMenuEntry;

const char* bridge_transport_name(BridgeTransportMode mode);
size_t bridge_menu_count(BridgeTransportMode mode);
const BridgeMenuEntry* bridge_menu_entry(BridgeTransportMode mode, size_t index);
bool bridge_menu_has_radio(BridgeTransportMode mode);
