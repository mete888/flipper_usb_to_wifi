#include "ble_pairing.h"
#include "bridge_protocol.h"
#include <stdio.h>
#include <string.h>

static bool equal_secret(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t different = 0;
    for(size_t i = 0; i < n; ++i) different |= a[i] ^ b[i];
    return different == 0;
}

void fib_ble_pairing_reset(FibBlePairing* p, uint64_t nonce) {
    memset(p, 0, sizeof(*p));
    p->nonce = nonce;
}

FibPairResult fib_ble_pairing_open(FibBlePairing* p, const uint8_t* data, size_t size,
    uint32_t now_ms, FibPairLoad load, void* context) {
    if(!p || !data || size != 56 || p->opened ||
       fib_read_u64_le(data) != p->nonce) return FibPairRejected;
    p->opened = true;
    memcpy(p->host, data + 8, 16);
    uint8_t saved[32] = {0}, empty[32] = {0};
    if(!equal_secret(data + 24, empty, 32) && load && load(context, p->host, saved) &&
       equal_secret(saved, data + 24, 32)) {
        memset(saved, 0, sizeof(saved));
        p->known_host = true;
    }
    memset(saved, 0, sizeof(saved));
    p->approval_pending = true;
    p->issued_ms = now_ms;
    return FibPairWaiting;
}

void fib_ble_pairing_reject(FibBlePairing* p) {
    if(!p) return;
    p->approval_pending = p->pending = p->accepted = false;
    memset(p->code, 0, sizeof(p->code));
}

FibPairResult fib_ble_pairing_approve(FibBlePairing* p, uint32_t random_code, uint32_t now_ms) {
    if(!p || !p->approval_pending) return FibPairRejected;
    if((uint32_t)(now_ms - p->issued_ms) >= FIB_PAIR_TIMEOUT_MS) {
        fib_ble_pairing_reject(p);
        return FibPairRejected;
    }
    if(!p->known_host && random_code >= 1000000U) return FibPairRejected;
    p->approval_pending = false;
    if(p->known_host) {
        p->accepted = true;
        return FibPairAccepted;
    }
    snprintf(p->code, sizeof(p->code), "%06lu", (unsigned long)random_code);
    p->issued_ms = now_ms;
    p->pending = true;
    return FibPairNeedsCode;
}

FibPairResult fib_ble_pairing_submit(FibBlePairing* p, const uint8_t* data, size_t size,
    uint32_t now_ms, FibPairSave save, void* context) {
    if(!p || !data || size != 62 || !p->pending || p->accepted ||
       fib_read_u64_le(data) != p->nonce || !equal_secret(p->host, data + 8, 16))
        return FibPairRejected;
    if((uint32_t)(now_ms - p->issued_ms) >= FIB_PAIR_TIMEOUT_MS || p->attempts >= 3) {
        p->pending = false;
        memset(p->code, 0, sizeof(p->code));
        return FibPairRejected;
    }
    ++p->attempts;
    if(!equal_secret((const uint8_t*)p->code, data + 24, 6)) {
        if(p->attempts < 3) return FibPairNeedsCode;
        p->pending = false;
        memset(p->code, 0, sizeof(p->code));
        return FibPairRejected;
    }
    uint8_t empty[32] = {0};
    if(equal_secret(empty, data + 30, 32) || !save || !save(context, p->host, data + 30)) {
        p->pending = false;
        memset(p->code, 0, sizeof(p->code));
        return FibPairRejected;
    }
    p->pending = false;
    p->accepted = true;
    memset(p->code, 0, sizeof(p->code));
    return FibPairAccepted;
}

bool fib_ble_pairing_revoke_matches(const FibBlePairing* p, const uint8_t* data, size_t size) {
    return p && data && size == 24 && p->accepted &&
        fib_read_u64_le(data) == p->nonce && equal_secret(p->host, data + 8, 16);
}

bool fib_pair_host_from_filename(const char* name, uint8_t host[16]) {
    if(!name || !host || strlen(name) != 36 || strcmp(name + 32, ".key")) return false;
    uint8_t decoded[16] = {0};
    for(size_t i = 0; i < 32; ++i) {
        char c = name[i];
        int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if(value < 0) return false;
        if(i % 2 == 0) decoded[i / 2] = (uint8_t)(value << 4);
        else decoded[i / 2] |= (uint8_t)value;
    }
    memcpy(host, decoded, sizeof(decoded));
    return true;
}
