from __future__ import annotations
import dataclasses
import io
import unittest
from threading import Event
from unittest.mock import patch, Mock

from host.fibp_host.network import NetworkRequestError, perform_request
from host.fibp_host.session import HostSession, PermissionDecision
from scripts.fibp_codec import (Frame, FrameFlags, MessageType, HeaderField,
    encode_header, encode_hello, decode_hello, encode_request_start, RequestStart, decode_hello_ack)
from tests.test_host_session import hello_frame


class BluetoothRadioTests(unittest.TestCase):
    def test_no_radio_capabilities_and_text_limit_in_handshake(self):
        session = HostSession(lambda _: self.fail("Unexpected request"), lambda _: True, bluetooth=True)
        hello = dataclasses.replace(decode_hello(hello_frame().payload), capabilities=0x39f)
        replies = session.on_frame(Frame(MessageType.HELLO, encode_hello(hello)))
        ack = decode_hello_ack(replies[0].payload)
        self.assertEqual(ack.capabilities & 0x380, 0)
        self.assertLessEqual(ack.maximum_response_bytes, 8192)

    def test_all_old_radio_profiles_rejected_before_network(self):
        for value in ["audio/mpeg", "audio/mpeg; fib-rate=32", "audio/mpeg; fib-pcm=4000", "audio/mpeg; fib-adpcm=4000"]:
            with self.subTest(profile=value):
                requests = []
                session = HostSession(requests.append, lambda _: True, bluetooth=True)
                session.on_frame(hello_frame())
                session.resolve_permission(PermissionDecision.ALLOW_ONCE)
                session.on_frame(Frame(MessageType.REQUEST_START,
                    encode_request_start(RequestStart(1, 30000, "https://example.com/live", 0, 1)), request_id=1))
                replies = session.on_frame(Frame(MessageType.REQUEST_HEADER,
                    encode_header(HeaderField("accept", value)), request_id=1, sequence=1))
                self.assertEqual(replies[0].message_type, MessageType.ERROR)
                self.assertIsNone(session.pending)
                self.assertFalse(requests)

    def test_network_layer_rejects_ble_audio_before_opening_socket(self):
        with patch("host.fibp_host.network._request_once") as connect:
            with self.assertRaises(NetworkRequestError):
                perform_request(RequestStart(1, 30000, "https://example.com/live"),
                    [HeaderField("accept", "audio/mpeg")], b"", maximum_bytes=65536,
                    cancel=Event(), on_start=lambda *_: None, on_chunk=lambda _: None, bluetooth=True)
            connect.assert_not_called()

    def test_usb_audio_is_unchanged_and_cancel_is_observed(self):
        payload = bytes(range(256)) * 128
        response = io.BytesIO(payload)
        response.status = 200
        response.getheaders = lambda: [("content-type", "audio/mpeg")]
        response.getheader = lambda name: "audio/mpeg" if name.lower() == "content-type" else None
        cancel, output = Event(), bytearray()
        def receive(data):
            output.extend(data)
            if len(output) >= 16384: cancel.set()
        with patch("host.fibp_host.network._request_once", return_value=(response, Mock())):
            result = perform_request(RequestStart(1, 30000, "https://example.com/live"),
                [HeaderField("accept", "audio/mpeg")], b"", maximum_bytes=65536,
                cancel=cancel, on_start=lambda *_: None, on_chunk=receive)
        self.assertTrue(result.cancelled)
        self.assertEqual(output, payload[:len(output)])
        self.assertGreaterEqual(len(output), 16384)
