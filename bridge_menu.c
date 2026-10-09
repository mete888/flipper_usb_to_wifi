#include "bridge_menu.h"

static const BridgeMenuEntry usb[] = {
    {"Test Connection", FibMenuTestConnection},
    {"Get Sample Text", FibMenuDownloadSample},
    {"Get Date and Time", FibMenuGetDateTime},
    {"Toolbox", FibMenuToolbox},
    {"Custom URL Request", FibMenuCustomUrl},
    {"Connection Info", FibMenuConnectionInfo},
};
static const BridgeMenuEntry bluetooth[] = {
    {"Test Connection", FibMenuTestConnection},
    {"Connection Requests", FibMenuBluetoothRequests},
    {"Pairings", FibMenuBluetoothPairings},
    {"Get Sample Text", FibMenuDownloadSample},
    {"Get Date and Time", FibMenuGetDateTime},
    {"Toolbox", FibMenuToolbox},
    {"Custom URL Request", FibMenuCustomUrl},
    {"Connection Info", FibMenuConnectionInfo},
};
const char* bridge_transport_name(BridgeTransportMode mode) {
    return mode == BridgeTransportUSB ? "USB Internet Bridge" :
           mode == BridgeTransportBluetooth ? "Bluetooth Internet Bridge" : "Internet Bridge";
}
size_t bridge_menu_count(BridgeTransportMode mode) {
    return mode == BridgeTransportUSB ? sizeof(usb) / sizeof(*usb) :
           mode == BridgeTransportBluetooth ? sizeof(bluetooth) / sizeof(*bluetooth) : 0U;
}
const BridgeMenuEntry* bridge_menu_entry(BridgeTransportMode mode, size_t index) {
    if(index >= bridge_menu_count(mode)) return NULL;
    return mode == BridgeTransportUSB ? &usb[index] : &bluetooth[index];
}
bool bridge_menu_has_radio(BridgeTransportMode mode) { return mode == BridgeTransportUSB; }
