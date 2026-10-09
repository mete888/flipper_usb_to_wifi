#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct FuriMutex FuriMutex;
typedef struct FuriMessageQueue FuriMessageQueue;
typedef enum { FuriMutexTypeNormal } FuriMutexType;
typedef enum { FuriStatusOk, FuriStatusError } FuriStatus;
#define FuriWaitForever UINT32_MAX
FuriMutex* furi_mutex_alloc(FuriMutexType);
void furi_mutex_free(FuriMutex*);
FuriStatus furi_mutex_acquire(FuriMutex*, uint32_t);
FuriStatus furi_mutex_release(FuriMutex*);
FuriMessageQueue* furi_message_queue_alloc(uint32_t, uint32_t);
void furi_message_queue_free(FuriMessageQueue*);
FuriStatus furi_message_queue_put(FuriMessageQueue*, const void*, uint32_t);
FuriStatus furi_message_queue_get(FuriMessageQueue*, void*, uint32_t);
uint32_t furi_ms_to_ticks(uint32_t);
void* furi_record_open(const char*);
void furi_record_close(const char*);
