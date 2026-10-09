#include "decoder.h"
#include <stdlib.h>
#include <string.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "../../third_party/minimp3/minimp3.h"

#define INPUT_SIZE 4096U
#define OUTPUT_RATE 14493U
struct FibRadioDecoder {
    mp3dec_t mp3;
    uint8_t input[INPUT_SIZE];
    size_t used;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint32_t source_rate;
    uint32_t filled;
    int64_t sum;
    uint8_t output[512];
    size_t output_used;
};

FibRadioDecoder* fib_radio_decoder_alloc(void) {
    FibRadioDecoder* decoder = calloc(1, sizeof(*decoder));
    if(decoder) mp3dec_init(&decoder->mp3);
    return decoder;
}
void fib_radio_decoder_free(FibRadioDecoder* decoder) { free(decoder); }

static void flush(FibRadioDecoder* decoder, FibRadioPCMCallback callback, void* context) {
    if(decoder->output_used) callback(decoder->output, decoder->output_used, context);
    decoder->output_used = 0;
}

static void output_frame(FibRadioDecoder* decoder, const mp3dec_frame_info_t* info,
                         int samples, FibRadioPCMCallback callback, void* context) {
    if(info->hz <= 0 || info->channels < 1 || info->channels > 2) return;
    const uint32_t rate = (uint32_t)info->hz;
    if(decoder->source_rate != rate) {
        decoder->source_rate = rate;
        decoder->filled = 0;
        decoder->sum = 0;
    }
    for(int index = 0; index < samples; ++index) {
        int32_t mono = decoder->pcm[index * info->channels];
        if(info->channels == 2) mono = (mono + decoder->pcm[index * 2 + 1]) / 2;
        uint32_t remaining = OUTPUT_RATE;
        while(remaining) {
            const uint32_t room = rate - decoder->filled;
            const uint32_t weight = remaining < room ? remaining : room;
            decoder->sum += (int64_t)mono * weight;
            decoder->filled += weight;
            remaining -= weight;
            if(decoder->filled == rate) {
                const uint16_t sample = (uint16_t)(int16_t)(decoder->sum / (int32_t)rate);
                decoder->output[decoder->output_used++] = (uint8_t)sample;
                decoder->output[decoder->output_used++] = (uint8_t)(sample >> 8);
                decoder->filled = 0;
                decoder->sum = 0;
                if(decoder->output_used == sizeof(decoder->output)) flush(decoder, callback, context);
            }
        }
    }
}

void fib_radio_decoder_feed(FibRadioDecoder* decoder, const uint8_t* data, size_t length,
                            FibRadioPCMCallback callback, void* context) {
    if(!decoder || !callback || (!data && length)) return;
    while(length) {
        size_t count = INPUT_SIZE - decoder->used;
        if(count > length) count = length;
        memcpy(decoder->input + decoder->used, data, count);
        decoder->used += count;
        data += count;
        length -= count;
        while(decoder->used >= 1024U) {
            mp3dec_frame_info_t info;
            const int samples = mp3dec_decode_frame(&decoder->mp3, decoder->input,
                                                    (int)decoder->used, decoder->pcm, &info);
            if(info.frame_bytes > 0 && (size_t)info.frame_bytes <= decoder->used) {
                if(samples > 0) output_frame(decoder, &info, samples, callback, context);
                decoder->used -= (size_t)info.frame_bytes;
                memmove(decoder->input, decoder->input + info.frame_bytes, decoder->used);
            } else if(decoder->used == INPUT_SIZE) {
                memmove(decoder->input, decoder->input + 1, --decoder->used);
            } else break;
        }
    }
    flush(decoder, callback, context);
}
