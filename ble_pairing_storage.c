#include "ble_pairing_storage.h"
#include "ble_pairing.h"
#include "bridge_protocol.h"
#include <furi.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define PAIR_DIR EXT_PATH("apps_data/usb_internet_bridge/ble_pairs")

static void pair_path(char* path, size_t capacity, const uint8_t* host) {
    char id[33];
    for(size_t i = 0; i < 16; ++i) snprintf(id + i * 2, 3, "%02x", host[i]);
    snprintf(path, capacity, "%s/%s.key", PAIR_DIR, id);
}

bool fib_pair_storage_load(void* context, const uint8_t* host, uint8_t* token) {
    UNUSED(context);
    char path[128]; pair_path(path, sizeof(path), host);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint8_t data[56] = {0};
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
        storage_file_size(file) == sizeof(data) &&
        storage_file_read(file, data, sizeof(data)) == sizeof(data) &&
        memcmp(data, "FBP2", 4) == 0 && memcmp(data + 4, host, 16) == 0 &&
        fib_crc32(data, 52) == fib_read_u32_le(data + 52);
    if(ok) memcpy(token, data + 20, 32);
    memset(data, 0, sizeof(data));
    storage_file_close(file); storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool fib_pair_storage_save(void* context, const uint8_t* host, const uint8_t* token) {
    UNUSED(context);
    char path[128], temporary[136]; pair_path(path, sizeof(path), host);
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, EXT_PATH("apps_data/usb_internet_bridge"));
    storage_common_mkdir(storage, PAIR_DIR);
    File* file = storage_file_alloc(storage);
    uint8_t data[56];
    memcpy(data, "FBP2", 4); memcpy(data + 4, host, 16); memcpy(data + 20, token, 32);
    fib_write_u32_le(data + 52, fib_crc32(data, 52));
    bool ok = storage_file_open(file, temporary, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
        storage_file_write(file, data, sizeof(data)) == sizeof(data) && storage_file_sync(file);
    memset(data, 0, sizeof(data));
    storage_file_close(file); storage_file_free(file);
    if(ok) {
        /* Only this application's credential is replaced. A power-loss window
         * can require re-pairing; it must never authorize an unknown host. */
        storage_common_remove(storage, path);
        ok = storage_common_rename(storage, temporary, path) == FSE_OK;
    }
    if(!ok) storage_common_remove(storage, temporary);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool fib_pair_storage_remove(const uint8_t* host) {
    char path[128]; pair_path(path, sizeof(path), host);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error result = storage_common_remove(storage, path);
    furi_record_close(RECORD_STORAGE);
    return result == FSE_OK || result == FSE_NOT_EXIST;
}

size_t fib_pair_storage_list(uint8_t (*hosts)[16], size_t capacity, size_t offset, bool* more) {
    if(more) *more = false;
    if(!hosts || !capacity) return 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* directory = storage_file_alloc(storage);
    size_t used = 0, skipped = 0;
    char name[40]; FileInfo info;
    if(storage_dir_open(directory, PAIR_DIR)) {
        while(storage_dir_read(directory, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            uint8_t host[16], token[32];
            if(!fib_pair_host_from_filename(name, host) || !fib_pair_storage_load(NULL, host, token)) continue;
            memset(token, 0, sizeof(token));
            if(skipped++ < offset) continue;
            if(used == capacity) { if(more) *more = true; break; }
            memcpy(hosts[used++], host, sizeof(host));
        }
    }
    storage_dir_close(directory); storage_file_free(directory);
    furi_record_close(RECORD_STORAGE);
    return used;
}
