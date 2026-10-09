#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* BLE-only extension inside ordinary CRC-framed FIBP, before HELLO_ACK.
 * Bearer credentials MUST travel only over authenticated, encrypted GATT.
 * This is bridge recognition, not replacement Bluetooth link security. */
#define FIB_BLE_PAIR_CAPABILITY (1UL << 5)
#define FIB_BLE_SELECTION_CAPABILITY (1UL << 6)
#define FIB_PAIR_OPEN 0x40U
#define FIB_PAIR_CODE 0x41U
#define FIB_PAIR_NEEDED 0x42U
#define FIB_PAIR_RESULT 0x43U
#define FIB_PAIR_REVOKE 0x44U
#define FIB_PAIR_WAITING 0x45U
#define FIB_PAIR_FORGOTTEN 0x46U
#define FIB_PAIR_HOST_SIZE 16U
#define FIB_PAIR_TOKEN_SIZE 32U
#define FIB_PAIR_TIMEOUT_MS 120000U

typedef enum { FibPairRejected = 0, FibPairAccepted = 1, FibPairNeedsCode = 2,
    FibPairWaiting = 3 } FibPairResult;
typedef bool (*FibPairLoad)(void*, const uint8_t*, uint8_t*);
typedef bool (*FibPairSave)(void*, const uint8_t*, const uint8_t*);
typedef struct {
    uint64_t nonce;
    uint8_t host[16];
    char code[7];
    uint32_t issued_ms;
    uint8_t attempts;
    bool opened;
    bool approval_pending;
    bool known_host;
    bool pending;
    bool accepted;
} FibBlePairing;

void fib_ble_pairing_reset(FibBlePairing*, uint64_t nonce);
FibPairResult fib_ble_pairing_open(FibBlePairing*, const uint8_t*, size_t,
    uint32_t now_ms, FibPairLoad, void*);
FibPairResult fib_ble_pairing_approve(FibBlePairing*, uint32_t random_code, uint32_t now_ms);
void fib_ble_pairing_reject(FibBlePairing*);
FibPairResult fib_ble_pairing_submit(FibBlePairing*, const uint8_t*, size_t,
    uint32_t now_ms, FibPairSave, void*);
bool fib_ble_pairing_revoke_matches(const FibBlePairing*, const uint8_t*, size_t);
bool fib_pair_host_from_filename(const char*, uint8_t host[16]);
