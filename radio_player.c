#include "radio_player.h"
#include "radio_audio.h"
#include <stdlib.h>

/* MP3 and its 24 KiB stack now live on the host. Only the original 16-bit
 * speaker reservoir remains on Flipper. Stop wakes a blocked USB writer
 * before callback teardown takes the parser lock. */
struct RadioPlayer {
    RadioAudio* audio;
    volatile bool running;
    volatile bool stop_requested;
    bool have_low_byte;
    uint8_t low_byte;
    volatile uint32_t chunks;
    const char* error;
};

size_t radio_player_required_heap(void) {
    return radio_audio_required_heap() + sizeof(RadioPlayer) + 4096U;
}
RadioPlayer* radio_player_alloc(void) {
    RadioPlayer* player = calloc(1, sizeof(*player));
    if(player) player->audio = radio_audio_alloc();
    if(player && !player->audio) { free(player); return NULL; }
    return player;
}
bool radio_player_start(RadioPlayer* player) {
    if(!player || player->running) return false;
    /* Speaker ownership remains on the GUI/app thread. The USB worker writes
     * samples only; it never acquires/releases speaker ownership. */
    radio_audio_stop(player->audio);
    player->error = NULL;
    player->have_low_byte = false;
    player->chunks = 0;
    player->stop_requested = false;
    if(!radio_audio_start(player->audio, 100U)) {
        player->error = "Speaker or timer busy";
        return false;
    }
    player->running = true;
    return true;
}
void radio_player_request_stop(RadioPlayer* player) {
    if(!player) return;
    player->stop_requested = true;
    player->running = false;
    radio_audio_request_stop(player->audio);
}
void radio_player_stop(RadioPlayer* player) {
    if(!player) return;
    radio_player_request_stop(player);
    radio_audio_stop(player->audio);
}
void radio_player_free(RadioPlayer* player) {
    if(!player) return;
    radio_player_stop(player);
    radio_audio_free(player->audio);
    free(player);
}
bool radio_player_push(RadioPlayer* player, const uint8_t* data, size_t length) {
    if(!player || (!data && length) || !player->running) return false;
    for(size_t index = 0; index < length; ++index) {
        if(player->stop_requested) return false;
        if(!player->have_low_byte) {
            player->low_byte = data[index];
            player->have_low_byte = true;
        } else {
            const int16_t sample = (int16_t)((uint16_t)player->low_byte | (uint16_t)data[index] << 8);
            player->have_low_byte = false;
            if(!radio_audio_write(player->audio, sample)) return false;
        }
    }
    player->chunks++;
    return !player->stop_requested;
}
bool radio_player_is_running(const RadioPlayer* player) { return player && player->running; }
uint32_t radio_player_decoded_frames(const RadioPlayer* player) { return player ? player->chunks : 0U; }
size_t radio_player_buffered_bytes(RadioPlayer* player) {
    return player ? radio_audio_buffered_bytes(player->audio) : 0U;
}
uint32_t radio_player_underflows(const RadioPlayer* player) {
    return player ? radio_audio_underflows(player->audio) : 0U;
}
const char* radio_player_error(const RadioPlayer* player) { return player ? player->error : NULL; }
