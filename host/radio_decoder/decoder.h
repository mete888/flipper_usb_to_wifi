#pragma once
#include <stddef.h>
#include <stdint.h>

/* Same minimp3 and box resampler as the original FAP, now on the host.
 * The callback receives bounded s16le, mono, 14493 Hz blocks. No networking. */
typedef struct FibRadioDecoder FibRadioDecoder;
typedef void (*FibRadioPCMCallback)(const uint8_t*, size_t, void*);
FibRadioDecoder* fib_radio_decoder_alloc(void);
void fib_radio_decoder_free(FibRadioDecoder* decoder);
void fib_radio_decoder_feed(FibRadioDecoder* decoder, const uint8_t* data, size_t length,
FibRadioPCMCallback callback, void* context);
