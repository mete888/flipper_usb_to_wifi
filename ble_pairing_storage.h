#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool fib_pair_storage_load(void*, const uint8_t* host, uint8_t* token);
bool fib_pair_storage_save(void*, const uint8_t* host, const uint8_t* token);
bool fib_pair_storage_remove(const uint8_t* host);
/* Bounded page; only CRC-valid, exact app-owned credential records are listed. */
size_t fib_pair_storage_list(uint8_t (*hosts)[16], size_t capacity, size_t offset, bool* more);
