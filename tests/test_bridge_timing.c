#include "bridge_timing.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(!fib_ticks_elapsed(1000U, 1001U, 5000U));
    assert(!fib_ticks_elapsed(1000U, 1005U, 1U));
    assert(!fib_ticks_elapsed(5000U, 1U, 5000U));
    assert(fib_ticks_elapsed(5001U, 1U, 5000U));
    assert(fib_ticks_elapsed(20U, UINT32_MAX - 10U, 31U));
    assert(!fib_ticks_elapsed(20U, UINT32_MAX - 10U, 32U));
    assert(!fib_ticks_elapsed(UINT32_MAX - 2U, 1U, 1U));
    puts("timer progress race and counter wrap tests passed");
}
