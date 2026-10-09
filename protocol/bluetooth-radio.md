# Withdrawn Bluetooth radio experiment

Removed on 2026-10-07 at the user's request. This is not an active protocol
extension. Previous experimental capability bits 7, 8 and 9 (0x380) are
reserved: never advertise or reuse them for another meaning. Old radio
profiles (fib-rate, fib-pcm and fib-adpcm) are no longer supported.

Bluetooth Alpha negotiates at most 8 KiB and supports HTTPS text GET only.
Explicit audio requests fail before HTTP execution; audio responses fail
closed. USB uses the original bounded MP3 byte stream and on-Flipper decoder,
with no custom audio header, converter or FFmpeg dependency.

The FIBP framing, Pairings, manual Pair/Connect, and fresh internet permission
are unchanged. Earlier QA reports describe historical experiments only.
