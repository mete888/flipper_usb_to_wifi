#include "radio_player.h"
#include "radio_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct RadioAudio { bool active; bool abort; };
static int16_t received[32];
static size_t count;
static unsigned starts, stops;
static RadioPlayer* stop_during_write;
RadioAudio* radio_audio_alloc(void) { return calloc(1, sizeof(RadioAudio)); }
void radio_audio_free(RadioAudio* audio) { free(audio); }
size_t radio_audio_required_heap(void) { return 16496U; }
size_t radio_audio_buffered_bytes(const RadioAudio* audio) { (void)audio; return count * 2U; }
uint32_t radio_audio_underflows(const RadioAudio* audio) { (void)audio; return 0U; }
bool radio_audio_start(RadioAudio* audio, uint8_t volume) {
    assert(volume == 100U); audio->active = true; audio->abort = false; ++starts; return true;
}
void radio_audio_stop(RadioAudio* audio) { if(audio && audio->active) ++stops; if(audio) audio->active = false; }
void radio_audio_request_stop(RadioAudio* audio) { audio->abort = true; }
bool radio_audio_write(RadioAudio* audio, int16_t sample) {
    if(stop_during_write) { radio_player_request_stop(stop_during_write); stop_during_write = NULL; }
    if(!audio->active || audio->abort) return false;
    assert(count < 32U); received[count++] = sample; return true;
}
int main(void) {
    assert(radio_player_required_heap() < 22U * 1024U);
    RadioPlayer* player = radio_player_alloc();
    assert(player && radio_player_start(player));
    const uint8_t a[] = {0x34};
    const uint8_t b[] = {0x12, 0x00, 0x80, 0xff, 0x7f};
    assert(radio_player_push(player, a, sizeof(a)));
    assert(count == 0U);
    assert(radio_player_push(player, b, sizeof(b)));
    assert(count == 3U && received[0] == 0x1234 && received[1] == -32768 && received[2] == 32767);
    assert(radio_player_buffered_bytes(player) == 6U);
    radio_player_request_stop(player);
    radio_player_request_stop(player); /* repeated Stop/physical Back is harmless */
    assert(!radio_player_is_running(player) && !radio_player_push(player, b, sizeof(b)));
    assert(radio_player_start(player));
    stop_during_write = player;
    assert(!radio_player_push(player, b, sizeof(b)) && count == 3U);
    radio_player_stop(player);
    assert(radio_player_start(player));
    assert(radio_player_push(player, b, 2U)); /* no stale half sample after restart */
    assert(received[3] == 0x0012);
    radio_player_free(player);
    assert(starts == 3U && stops == 3U);
    puts("radio PCM/fragmentation/Stop/restart tests passed");
}
