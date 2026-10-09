#include "ble_pairing.h"
#include "ble_discovery_config.h"
#include "bridge_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t host[16], token[32]; bool present, refuse; } Vault;
static bool load(void* context, const uint8_t* host, uint8_t* token) {
    Vault* v = context;
    if(!v->present || memcmp(host, v->host, 16)) return false;
    memcpy(token, v->token, 32); return true;
}
static bool save(void* context, const uint8_t* host, const uint8_t* token) {
    Vault* v = context;
    if(v->refuse) return false;
    memcpy(v->host, host, 16); memcpy(v->token, token, 32); v->present = true; return true;
}
static uint8_t opening[56], submission[62];
static void inputs(void) {
    memset(opening, 0, sizeof(opening));
    fib_write_u64_le(opening, 0x0807060504030201ULL);
    for(size_t i = 0; i < 16; ++i) opening[8 + i] = (uint8_t)(i + 1);
    memcpy(submission, opening, 24);
    memcpy(submission + 24, "123456", 6); memset(submission + 30, 7, 32);
}
static FibFrame decoded;
static size_t frames;
static void on_frame(const FibFrame* f, void* context) { (void)context; decoded = *f; ++frames; }
static void on_error(FibParseError e, void* context) { (void)context; (void)e; assert(!"invalid CRC frame"); }
static FibPairResult open_code(FibBlePairing* p, const uint8_t* data, size_t size,
    uint32_t random, uint32_t now, FibPairLoad loader, void* context) {
    FibPairResult result = fib_ble_pairing_open(p, data, size, now, loader, context);
    return result == FibPairWaiting ? fib_ble_pairing_approve(p, random, now) : result;
}

int main(void) {
    uint8_t host[16]; memset(host, 7, sizeof(host));
    assert(fib_pair_host_from_filename("00112233445566778899aabbccddeeff.key", host));
    assert(host[0] == 0 && host[15] == 255);
    assert(!fib_pair_host_from_filename("00112233445566778899aabbccddeeff.key.new", host));
    assert(!fib_pair_host_from_filename("../112233445566778899aabbccddeeff.key", host));
    assert(!fib_pair_host_from_filename("00112233445566778899aabbccddeeGG.key", host));
    assert(!fib_pair_host_from_filename(NULL, host));
    assert(FIB_BLE_ADV_BYTES == 28U && FIB_BLE_ADV_BYTES <= 31U);
    assert(3U + 3U + 2U + FIB_BLE_ADV_NAME_MAX + 2U + 16U > 31U);
    inputs(); FibBlePairing p; Vault v = {0};
    const uint64_t nonce = 0x0807060504030201ULL;
    fib_ble_pairing_reset(&p, nonce);
    assert(fib_ble_pairing_open(&p, opening, 56, 100, load, &v) == FibPairWaiting);
    assert(p.approval_pending && !p.pending && !p.accepted && p.code[0] == 0);
    assert(!p.known_host);
    assert(fib_ble_pairing_submit(&p, submission, 62, 101, save, &v) == FibPairRejected);
    assert(fib_ble_pairing_approve(&p, 123456, 102) == FibPairNeedsCode);
    assert(p.pending && !p.accepted && !strcmp(p.code, "123456"));
    assert(fib_ble_pairing_open(&p, opening, 56, 100, load, &v) == FibPairRejected);
    assert(fib_ble_pairing_submit(&p, submission, 62, 200, save, &v) == FibPairAccepted);
    assert(p.accepted && !p.pending && p.code[0] == 0 && v.present);
    assert(fib_ble_pairing_revoke_matches(&p, opening, 24));
    assert(!fib_ble_pairing_revoke_matches(&p, opening, 23));
    opening[8] ^= 1;
    assert(!fib_ble_pairing_revoke_matches(&p, opening, 24)); opening[8] ^= 1;
    /* Known host/token resumes. Zero token after Mac deletion requires code,
     * even though the disconnected Flipper still has its old credential. */
    memcpy(opening + 24, v.token, 32); fib_ble_pairing_reset(&p, nonce);
    assert(fib_ble_pairing_open(&p, opening, 56, 0, load, &v) == FibPairWaiting);
    assert(p.known_host && p.approval_pending && !p.accepted && p.code[0] == 0);
    assert(fib_ble_pairing_approve(&p, 0, 1) == FibPairAccepted);
    memset(opening + 24, 0, 32); fib_ble_pairing_reset(&p, nonce);
    assert(open_code(&p, opening, 56, 123456, 0, load, &v) == FibPairNeedsCode);
    /* Three attempts and expiry are fail-closed. */
    submission[24] = '9';
    assert(fib_ble_pairing_submit(&p, submission, 62, 1, save, &v) == FibPairNeedsCode);
    assert(fib_ble_pairing_submit(&p, submission, 62, 2, save, &v) == FibPairNeedsCode);
    assert(fib_ble_pairing_submit(&p, submission, 62, 3, save, &v) == FibPairRejected);
    assert(!p.pending && !p.accepted && p.code[0] == 0);
    submission[24] = '1';
    fib_ble_pairing_reset(&p, nonce);
    assert(open_code(&p, opening, 56, 123456, 0, load, &v) == FibPairNeedsCode);
    assert(fib_ble_pairing_submit(&p, submission, 62, FIB_PAIR_TIMEOUT_MS, save, &v) == FibPairRejected);
    /* Tick wrap, stale nonce, malformed sizes, persistence failure. */
    fib_ble_pairing_reset(&p, nonce);
    assert(open_code(&p, opening, 56, 123456, UINT32_MAX - 50U, load, &v) == FibPairNeedsCode);
    assert(fib_ble_pairing_submit(&p, submission, 62, 100U, save, &v) == FibPairAccepted);
    fib_ble_pairing_reset(&p, nonce + 1);
    assert(fib_ble_pairing_open(&p, opening, 56, 0, load, &v) == FibPairRejected);
    fib_ble_pairing_reset(&p, nonce);
    assert(fib_ble_pairing_open(&p, opening, 55, 0, load, &v) == FibPairRejected);
    assert(fib_ble_pairing_open(&p, opening, 56, 0, load, &v) == FibPairWaiting);
    assert(fib_ble_pairing_approve(&p, 1000000, 0) == FibPairRejected);
    assert(fib_ble_pairing_approve(&p, 123456, 0) == FibPairNeedsCode);
    assert(fib_ble_pairing_submit(&p, submission, 61, 1, save, &v) == FibPairRejected);
    v.refuse = true;
    assert(fib_ble_pairing_submit(&p, submission, 62, 1, save, &v) == FibPairRejected);
    assert(!p.accepted);
    v.refuse = false; fib_ble_pairing_reset(&p, nonce);
    assert(open_code(&p, opening, 56, 123456, 0, load, &v) == FibPairNeedsCode);
    memset(submission + 30, 0, 32);
    assert(fib_ble_pairing_submit(&p, submission, 62, 1, save, &v) == FibPairRejected);
    /* Reject and expiry do not create a code/token or reopen this session. */
    fib_ble_pairing_reset(&p, nonce);
    assert(fib_ble_pairing_open(&p, opening, 56, 0, load, &v) == FibPairWaiting);
    fib_ble_pairing_reject(&p);
    assert(!p.accepted && !p.pending && !p.approval_pending && p.code[0] == 0);
    assert(fib_ble_pairing_approve(&p, 123456, 1) == FibPairRejected);
    assert(fib_ble_pairing_open(&p, opening, 56, 1, load, &v) == FibPairRejected);
    fib_ble_pairing_reset(&p, nonce);
    assert(fib_ble_pairing_open(&p, opening, 56, 0, load, &v) == FibPairWaiting);
    assert(fib_ble_pairing_approve(&p, 123456, FIB_PAIR_TIMEOUT_MS) == FibPairRejected);
    assert(!p.approval_pending && p.code[0] == 0);
    fib_ble_pairing_reset(&p, nonce);
    assert(fib_ble_pairing_open(&p, opening, 56, 0, load, &v) == FibPairWaiting);
    assert(fib_ble_pairing_approve(&p, 123456, FIB_PAIR_TIMEOUT_MS - 1) == FibPairNeedsCode);
    assert(p.issued_ms == FIB_PAIR_TIMEOUT_MS - 1);
    assert(fib_ble_pairing_approve(&p, 123456, FIB_PAIR_TIMEOUT_MS) == FibPairRejected);
    /* Actual raw extension uses the normal framed byte stream (20-byte ATT
     * chunks); control sequence remains zero until ordinary HELLO_ACK. */
    uint8_t wire[128]; FibFrame frame = {.major = 1, .minor = 0,
        .type = (FibMessageType)FIB_PAIR_OPEN, .payload_length = sizeof(opening)};
    memcpy(frame.payload, opening, sizeof(opening));
    size_t n = 0; assert(fib_frame_encode(&frame, wire, sizeof(wire), &n));
    assert(n == 88); FibParser parser; fib_parser_init(&parser, on_frame, on_error, NULL);
    for(size_t i = 0; i < n; i += 20) fib_parser_feed(&parser, wire + i, n - i < 20 ? n - i : 20);
    assert(frames == 1 && decoded.type == (FibMessageType)FIB_PAIR_OPEN && decoded.sequence == 0);
    assert(decoded.payload_length == 56 && !memcmp(decoded.payload, opening, 56));
    puts("BLE pairing: recognition, revoke, retries, expiry, storage failure and framing passed");
    return 0;
}
