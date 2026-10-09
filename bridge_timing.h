#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Deadlines here are shorter than half the tick counter range. A callback
 * may update `then` after the caller sampled `now` but before it got a lock.
 * Treat that as recent progress, not a wrapped, enormous elapsed duration. */
static inline bool fib_ticks_elapsed(uint32_t now, uint32_t then, uint32_t duration) {
    const uint32_t elapsed = now - then;
    return elapsed <= INT32_MAX && elapsed >= duration;
}
