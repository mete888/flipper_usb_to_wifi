#pragma once

/* Reuse the firmware serial profile's existing 16-bit discovery marker, not
 * a made-up assigned service UUID. Mac additionally requires the bridge name
 * and validates authenticated GATT + bridge HELLO/code before internet access.
 * A 128-bit marker plus this name exceeds the legacy 31-byte ADV budget. */
#define FIB_BLE_DISCOVERY_UUID16 0x3080U
#define FIB_BLE_ADV_NAME_MAX 16U
#define FIB_BLE_ADV_BYTES (3U + 3U + (2U + FIB_BLE_ADV_NAME_MAX) + (2U + 2U))
_Static_assert(FIB_BLE_ADV_BYTES <= 31U, "BLE advertisement exceeds 31-byte budget");
