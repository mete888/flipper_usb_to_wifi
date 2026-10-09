from __future__ import annotations
import io
import os
import unittest
from pathlib import Path
from threading import Event, enumerate as threads
from unittest.mock import Mock, patch
from host.fibp_host import radio_pcm
from scripts.fibp_codec import (Hello, Capability, encode_hello, decode_hello,
    HelloAck, encode_hello_ack, decode_hello_ack, ResponseEnd, encode_response_end)


class FakeConnection:
    def settimeout(self, timeout): self.timeout = timeout
    def shutdown(self, how): self.closed = True
    def close(self): self.closed = True


class RadioPCMTests(unittest.TestCase):
    def test_host_fragments_decoder_blocks_to_192_byte_wire_chunks(self):
        from host.fibp_host.cli import BridgeHost
        from host.fibp_host.network import FetchResult
        from host.fibp_host.session import CompletedRequest
        from scripts.fibp_codec import RequestStart, MessageType
        host = BridgeHost(Mock(), None, False)
        request = CompletedRequest(7, RequestStart(1, 1000, "https://example.com/"), (), b"", 4096, 1, Event())
        payload = bytes(range(256)) * 2
        def fetch(*args):
            args[5](200, [], 0xffffffff)
            args[6](payload)
            return FetchResult(len(payload), False, False)
        with patch("host.fibp_host.cli.perform_request", side_effect=fetch):
            host._perform_request(request)
        frames = []
        while not host.outgoing.empty(): frames.append(host.outgoing.get_nowait())
        chunks = [f for f in frames if f.message_type == MessageType.RESPONSE_BODY_CHUNK]
        self.assertEqual([len(f.payload) for f in chunks], [192, 192, 128])
        self.assertEqual([f.sequence for f in chunks], [1, 2, 3])
        self.assertEqual(b"".join(f.payload for f in chunks), payload)

    def test_radio_budget_is_explicitly_negotiated(self):
        caps = Capability.HTTPS_GET | Capability.CANCELLATION | Capability.USB_RADIO_PCM
        hello = Hello(1, 0, 1, 0, caps, 512, radio_pcm.PCM_LIMIT, 1,
            "Flipper Zero", "Test", 1, b"123456", "0.5")
        self.assertEqual(decode_hello(encode_hello(hello)), hello)
        ack = HelloAck(1, 0, caps, 512, radio_pcm.PCM_LIMIT, 1, 2)
        self.assertEqual(decode_hello_ack(encode_hello_ack(ack)), ack)
        with self.assertRaises(ValueError):
            encode_hello_ack(HelloAck(1, 0, Capability.HTTPS_GET, 512, radio_pcm.PCM_LIMIT, 1, 2))
        with self.assertRaises(ValueError):
            encode_response_end(ResponseEnd(0, 5 * 1024 * 1024))
        self.assertEqual(len(encode_response_end(ResponseEnd(0, 5 * 1024 * 1024), radio_pcm.PCM_LIMIT)), 5)

    @unittest.skipUnless(radio_pcm.AVAILABLE, "compiled host radio decoder not installed")
    def test_malformed_blocks_and_limit(self):
        decoder = radio_pcm._radio_decoder.allocate()
        self.assertEqual(radio_pcm._radio_decoder.feed(decoder, bytes(4096)), b"")
        with self.assertRaises(ValueError):
            radio_pcm._radio_decoder.feed(decoder, bytes(4097))

    @unittest.skipUnless(radio_pcm.AVAILABLE, "compiled host radio decoder not installed")
    def test_cancel_during_buffering_joins_reader(self):
        cancel = Event(); cancel.set()
        connection = FakeConnection()
        self.assertEqual(radio_pcm.stream_pcm(io.BytesIO(bytes(4096)), connection, cancel,
            65536, 1, lambda _: self.fail("cancelled stream must not deliver audio")), (0, False, True))
        self.assertTrue(connection.closed)
        self.assertFalse(any(t.name == "FIBRadioPCM" for t in threads()))

    @unittest.skipUnless(radio_pcm.AVAILABLE and os.environ.get("FIB_RADIO_FIXTURE"), "MP3 fixture required")
    def test_fixture_chunking_size_limit_and_cancel(self):
        mp3 = Path(os.environ["FIB_RADIO_FIXTURE"]).read_bytes()
        connection = FakeConnection(); out = bytearray()
        result = radio_pcm.stream_pcm(io.BytesIO(mp3), connection, Event(), 32768, 2, out.extend)
        self.assertFalse(result[2]); self.assertEqual(result[0], len(out))
        self.assertGreater(len(out), 20000); self.assertEqual(len(out) % 2, 0)
        self.assertTrue(any(out)); self.assertTrue(connection.closed)
        cancel = Event()
        def chunk(_): cancel.set()
        result = radio_pcm.stream_pcm(io.BytesIO(mp3), FakeConnection(), cancel, 32768, 2, chunk)
        self.assertTrue(result[2]); self.assertFalse(any(t.name == "FIBRadioPCM" for t in threads()))
        result = radio_pcm.stream_pcm(io.BytesIO(mp3), FakeConnection(), Event(), 1000, 2, lambda _: None)
        self.assertEqual(result, (1000, True, False))
