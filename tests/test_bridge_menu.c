#include "bridge_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_menu_order(
    BridgeTransportMode mode, const BridgeMenuEntry* expected, size_t count) {
    assert(bridge_menu_count(mode) == count);
    for(size_t i = 0; i < count; ++i) {
        const BridgeMenuEntry* entry = bridge_menu_entry(mode, i);
        assert(entry != NULL);
        assert(entry->action == expected[i].action);
        assert(!strcmp(entry->label, expected[i].label));
    }
    assert(bridge_menu_entry(mode, count) == NULL);
}

int main(void) {
    assert(!strcmp(bridge_transport_name(BridgeTransportNone), "Internet Bridge"));
    assert(!strcmp(bridge_transport_name(BridgeTransportUSB), "USB Internet Bridge"));
    assert(!strcmp(bridge_transport_name(BridgeTransportBluetooth), "Bluetooth Internet Bridge"));
    assert(bridge_menu_count(BridgeTransportNone) == 0U);
    assert(bridge_menu_count(BridgeTransportUSB) == 6U);
    assert(bridge_menu_count(BridgeTransportBluetooth) == 8U);
    assert(bridge_menu_entry(BridgeTransportNone, 0U) == NULL);
    assert(bridge_menu_entry(BridgeTransportUSB, 6U) == NULL);
    assert(bridge_menu_has_radio(BridgeTransportUSB));
    assert(!bridge_menu_has_radio(BridgeTransportBluetooth));
    for(size_t i = 0; i < bridge_menu_count(BridgeTransportUSB); ++i) {
        assert(bridge_menu_entry(BridgeTransportUSB, i)->action != FibMenuBluetoothRequests);
        assert(bridge_menu_entry(BridgeTransportUSB, i)->action != FibMenuBluetoothPairings);
    }
    // Verify both order and actions: moving a row must not change what it opens.
    static const BridgeMenuEntry expected_usb[] = {
        {"Test Connection", FibMenuTestConnection},
        {"Get Sample Text", FibMenuDownloadSample},
        {"Get Date and Time", FibMenuGetDateTime},
        {"Toolbox", FibMenuToolbox},
        {"Custom URL Request", FibMenuCustomUrl},
        {"Connection Info", FibMenuConnectionInfo},
    };
    static const BridgeMenuEntry expected_bluetooth[] = {
        {"Test Connection", FibMenuTestConnection},
        {"Connection Requests", FibMenuBluetoothRequests},
        {"Pairings", FibMenuBluetoothPairings},
        {"Get Sample Text", FibMenuDownloadSample},
        {"Get Date and Time", FibMenuGetDateTime},
        {"Toolbox", FibMenuToolbox},
        {"Custom URL Request", FibMenuCustomUrl},
        {"Connection Info", FibMenuConnectionInfo},
    };
    assert_menu_order(BridgeTransportUSB, expected_usb,
                      sizeof(expected_usb) / sizeof(expected_usb[0]));
    assert_menu_order(BridgeTransportBluetooth, expected_bluetooth,
                      sizeof(expected_bluetooth) / sizeof(expected_bluetooth[0]));
    puts("transport-specific menu and USB-only radio tests passed");
}
