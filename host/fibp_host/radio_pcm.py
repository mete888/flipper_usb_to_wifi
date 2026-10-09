"""Bounded USB PCM delivery. Only consumes an already policy-validated HTTPS stream."""
from __future__ import annotations
import socket
import time
from queue import Queue, Empty, Full
from threading import Event, Thread

try:
    from . import _radio_decoder
except ImportError:
    _radio_decoder = None

AVAILABLE = _radio_decoder is not None
PCM_TYPE = "audio/x-fib-pcm;rate=14493;channels=1;format=s16le"
PCM_ACCEPT = "audio/mpeg; fib-pcm=14493"
PCM_RATE = 14493 * 2
PCM_LIMIT = 64 * 1024 * 1024


def stream_pcm(response, connection, cancel, maximum, timeout, on_chunk):
    if not AVAILABLE:
        raise RuntimeError("USB radio decoder missing; reinstall the updated desktop helper")
    queue = Queue(maxsize=128)  # At most 1 MiB, in <= 8192-byte pieces.
    stop = Event()
    end = object()
    decoder = _radio_decoder.allocate()

    def put(value):
        while not stop.is_set() and not cancel.is_set():
            try:
                queue.put(value, timeout=0.1)
                return
            except Full:
                continue

    def read():
        try:
            while not stop.is_set() and not cancel.is_set():
                source = response.read1(4096)
                if not source:
                    break
                pcm = _radio_decoder.feed(decoder, source)
                for offset in range(0, len(pcm), 8192):
                    put(pcm[offset:offset + 8192])
            put(end)
        except Exception as error:
            put(error)

    connection.settimeout(min(timeout, 10))
    worker = Thread(target=read, name="FIBRadioPCM", daemon=True)
    worker.start()
    buffered = bytearray()
    ended = False
    sent = 0
    started = False
    deadline = time.monotonic() + timeout
    next_tick = time.monotonic()
    try:
        while not cancel.is_set():
            if not started:
                if time.monotonic() >= deadline:
                    raise TimeoutError("radio buffering timed out")
                try:
                    block = queue.get(timeout=0.05)
                except Empty:
                    continue
                if isinstance(block, Exception):
                    raise block
                if block is end:
                    ended = True
                else:
                    buffered.extend(block)
                if len(buffered) < PCM_RATE * 10 and not ended:
                    continue
                if not buffered:
                    raise ValueError("No MP3 audio frames received")
                started = True
                next_tick = time.monotonic()
            # Keep the ten-second reservoir topped up without blocking audio
            # on a network read. Decoder/network and delivery have separate roles.
            while len(buffered) < PCM_RATE * 10 and not ended:
                try:
                    block = queue.get_nowait()
                except Empty:
                    break
                if isinstance(block, Exception):
                    raise block
                if block is end:
                    ended = True
                else:
                    buffered.extend(block)
            if not buffered:
                if ended:
                    return sent, False, False
                if cancel.wait(0.01):
                    break
                continue
            count = min(len(buffered), 464, maximum - sent)
            if count <= 0:
                return sent, True, False
            if cancel.wait(max(0, next_tick - time.monotonic())):
                break
            on_chunk(bytes(buffered[:count]))
            del buffered[:count]
            sent += count
            # Advance the original schedule: adding callback/queue overhead to
            # every interval makes delivery permanently slower than playback.
            # Bound catch-up to 100 ms after a long backpressure pause.
            next_tick = max(next_tick + count / PCM_RATE, time.monotonic() - 0.1)
        return sent, False, True
    finally:
        stop.set()
        # Wake a blocked SSL read before joining; never leave old decoders alive
        # after Cancel, disconnect, or changing station.
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        connection.close()
        worker.join(timeout=2)
