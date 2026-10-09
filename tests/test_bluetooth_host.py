from __future__ import annotations
import asyncio
import dataclasses
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
from types import SimpleNamespace

from host.fibp_host.bluetooth_pairing import (
    PairingClient, PairingStore, PairingError, KeyringVault, CAPABILITIES, WAITING, NEEDED, RESULT, FORGOTTEN)
from host.fibp_host.bluetooth_transport import BluetoothTransport, bridge_advertisement, DISCOVERY, RX
from host.fibp_host.bluetooth_demo import serve_demo
from host.fibp_host.session import HostSession, PermissionDecision
from scripts.fibp_codec import (
    Frame, FrameFlags, MessageType, RequestStart, encode_hello, decode_hello,
    encode_frame, encode_request_start, decode_hello_ack, HeaderField, encode_header)
from tests.test_host_session import hello_frame


class Vault:
    def __init__(self):
        self.values = {}
        self.refuse = False
    def get(self, key): return self.values.get(key)
    def set(self, key, value):
        if self.refuse: raise OSError("locked keyring")
        self.values[key] = value
    def delete(self, key):
        if self.refuse: raise OSError("locked keyring")
        self.values.pop(key, None)


def hello():
    value = dataclasses.replace(decode_hello(hello_frame().payload), capabilities=CAPABILITIES | 0x1F)
    return Frame(MessageType.HELLO, encode_hello(value))


class PairingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.vault = Vault()
        self.store = PairingStore(Path(self.temp.name) / "pairs.json", self.vault)
        self.now = 100.0
        self.client = PairingClient(self.store, lambda: self.now)
        self.opening = self.client.begin(hello())
    def reply(self, kind, status):
        return Frame(kind, self.opening.payload[:8] + bytes([status]))
    def select(self):
        self.assertEqual(self.client.receive(self.reply(WAITING, 3)), "waiting")
        self.assertFalse(self.client.accepted)
        self.assertEqual(self.client.receive(self.reply(NEEDED, 2)), "code")
    def establish(self):
        self.select()
        code = self.client.submit("123456")
        self.assertEqual(self.client.receive(self.reply(RESULT, 1)), "accepted")
        return code
    def test_new_known_and_offline_revoke_require_selection(self):
        self.assertEqual(len(self.opening.payload), 56)
        self.assertEqual(self.opening.payload[-32:], bytes(32))
        with self.assertRaises(PairingError): self.client.receive(self.reply(RESULT, 1))
        code = self.establish()
        self.assertEqual(len(code.payload), 62)
        self.assertEqual(self.store.token(self.client.hello), code.payload[-32:])
        resumed = PairingClient(self.store, lambda: self.now)
        self.assertEqual(resumed.begin(hello()).payload[-32:], code.payload[-32:])
        with self.assertRaises(PairingError): resumed.receive(self.reply(RESULT, 1))
        resumed.receive(self.reply(WAITING, 3))
        resumed.receive(self.reply(RESULT, 1))
        self.assertTrue(resumed.accepted)
        self.store.revoke(self.store.key(self.client.hello))
        self.assertFalse(self.store.records())
        self.assertFalse(self.vault.values)
        fresh = PairingClient(self.store, lambda: self.now)
        self.assertEqual(fresh.begin(hello()).payload[-32:], bytes(32))
    def test_wrong_order_codes_replay_nonce_and_revocation_notice(self):
        for code in ("123456", "abcdef", "１２３４５６"):
            with self.assertRaises(PairingError): self.client.submit(code)
        self.select()
        with self.assertRaises(PairingError): self.client.receive(self.reply(WAITING, 3))
        wrong = Frame(RESULT, b"BADNONCE\1")
        with self.assertRaises(PairingError): self.client.receive(wrong)
        notice = Frame(FORGOTTEN, self.opening.payload[:24])
        self.assertTrue(self.client.forgotten(notice))
        self.assertFalse(self.client.forgotten(dataclasses.replace(notice, sequence=1)))
        self.assertFalse(self.client.forgotten(Frame(FORGOTTEN, bytes(24))))
    def test_separate_deadlines_and_wrong_retry_does_not_renew(self):
        self.client.receive(self.reply(WAITING, 3))
        self.now += 119
        self.client.receive(self.reply(NEEDED, 2))
        self.assertEqual(self.client.deadline, 339)
        self.client.submit("111111")
        self.now += 5
        self.client.receive(self.reply(NEEDED, 2))
        self.assertEqual(self.client.deadline, 339)
        self.now = 339
        with self.assertRaises(PairingError): self.client.submit("123456")
    def test_persistence_uid_change_and_locked_vault_fail_closed(self):
        self.establish()
        loaded = PairingStore(self.store.path, self.vault)
        self.assertEqual(loaded.host_id, self.store.host_id)
        self.assertEqual(loaded.records(), self.store.records())
        changed = dataclasses.replace(self.client.hello, device_id=b"OTHER")
        self.assertIsNone(loaded.token(changed))
        self.vault.refuse = True
        with self.assertRaises(OSError): loaded.revoke(loaded.key(self.client.hello))
        self.assertTrue(loaded.records())
    def test_deny_legacy_and_do_not_store_failed_pair(self):
        old = dataclasses.replace(decode_hello(hello().payload), capabilities=0x20)
        with self.assertRaises(PairingError):
            PairingClient(self.store).begin(Frame(MessageType.HELLO, encode_hello(old)))
        self.select(); self.client.submit("123456"); self.vault.refuse = True
        with self.assertRaises(OSError): self.client.receive(self.reply(RESULT, 1))
        self.assertFalse(self.client.accepted)
        self.assertFalse(self.store.records())
    def test_plaintext_keyring_backend_is_rejected(self):
        class Plaintext:
            pass
        Plaintext.__module__ = "keyrings.alt.file"
        with patch.dict("sys.modules", {"keyring": SimpleNamespace(get_keyring=lambda: Plaintext())}):
            with self.assertRaises(PairingError): KeyringVault()
    def test_metadata_failure_does_not_leave_an_in_memory_pairing(self):
        self.select(); self.client.submit("123456")
        with patch.object(self.store, "_save", side_effect=OSError("read-only metadata")):
            with self.assertRaises(OSError): self.client.receive(self.reply(RESULT, 1))
        self.assertFalse(self.client.accepted)
        self.assertFalse(self.store.records())
        self.assertIsNone(self.store.token(self.client.hello))
    def test_bluetooth_consent_ignores_usb_grants_and_limits_requests(self):
        requests = []
        session = HostSession(requests.append, lambda _: True, bluetooth=True)
        ack = session.on_frame(hello_frame())[0]
        self.assertTrue(session.permission_pending)
        self.assertEqual(decode_hello_ack(ack.payload).maximum_response_bytes, 8192)
        session.resolve_permission(PermissionDecision.ALLOW_ALWAYS)
        self.assertEqual(session.permission, PermissionDecision.DENY)
        session.on_frame(hello_frame())
        session.resolve_permission(PermissionDecision.ALLOW_ONCE)
        post = Frame(MessageType.REQUEST_START, encode_request_start(RequestStart(2, 10000, "https://example.com")), request_id=10)
        self.assertEqual(session.on_frame(post)[0].message_type, MessageType.ERROR)
        start = Frame(MessageType.REQUEST_START, encode_request_start(RequestStart(1, 10000, "https://example.com", 0, 1)), request_id=11)
        session.on_frame(start)
        radio = Frame(MessageType.REQUEST_HEADER, encode_header(HeaderField("Accept", "audio/mpeg")), request_id=11, sequence=1)
        self.assertEqual(session.on_frame(radio)[0].message_type, MessageType.ERROR)
        self.assertFalse(requests)


class FakeClient:
    is_connected = True
    def __init__(self): self.writes = []
    async def write_gatt_char(self, char, data, response):
        self.writes.append((char, bytes(data), response))
    async def disconnect(self): self.is_connected = False


class TransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_new_code_consent_get_then_flipper_revoke_cancels_work(self):
        from host.fibp_host.cli import BridgeHost
        with tempfile.TemporaryDirectory() as directory:
            store = PairingStore(Path(directory) / "pairs.json", Vault())
            prefix = decode_hello(hello().payload).client_nonce.to_bytes(8, "little")
            replies = [hello(), Frame(WAITING, prefix + b"\3"), Frame(NEEDED, prefix + b"\2")]
            requests = []
            class Peer:
                def __init__(self): self.sent = []; self.noticed = False
                async def send(self, frame):
                    self.sent.append(frame)
                    if frame.message_type == 0x41:
                        replies.append(Frame(RESULT, prefix + b"\1"))
                    if frame.message_type == MessageType.PERMISSION_STATUS:
                        self.assert_granted = frame.payload[0] == 1
                        replies.extend([
                            Frame(MessageType.REQUEST_START, encode_request_start(RequestStart(1, 10000, "https://example.com")), request_id=99),
                            Frame(MessageType.REQUEST_END, request_id=99, sequence=1, flags=FrameFlags.FINAL),
                            Frame(FORGOTTEN, prefix + store.host_id)])
                async def read(self):
                    if replies: return encode_frame(replies.pop(0))
                    await asyncio.sleep(0)
                    return b""
            prompts = []
            def prompt(text, secret=False):
                prompts.append(secret)
                future = asyncio.get_running_loop().create_future()
                future.set_result("123456" if secret else "o")
                return future
            peer = Peer()
            with patch.object(BridgeHost, "_start_request", lambda _self, request: requests.append(request)):
                await asyncio.wait_for(serve_demo(peer, store, prompt=prompt), 1)
            self.assertEqual(prompts, [True, False])
            self.assertTrue(peer.assert_granted)
            self.assertEqual(len(requests), 1)
            self.assertEqual(requests[0].maximum_response_bytes, 8192)
            self.assertTrue(requests[0].cancel.is_set())
            self.assertFalse(store.records())
            self.assertFalse(store.vault.values)
    async def test_crc_chunks_credits_and_queue_overflow(self):
        client = FakeClient(); transport = BluetoothTransport(client)
        transport.flow_update(None, (2048).to_bytes(4, "big"))
        frame = Frame(0x40, bytes(56))
        await transport.send(frame)
        self.assertEqual(b"".join(data for _, data, _ in client.writes), encode_frame(frame))
        self.assertTrue(all(char == RX and len(data) <= 20 and response for char, data, response in client.writes))
        for _ in range(65): transport.receive(None, b"a")
        self.assertTrue(transport.closed.is_set())
        self.assertEqual(transport.incoming.qsize(), 64)
    async def test_invalid_credits_disconnect_and_no_writes(self):
        client = FakeClient(); transport = BluetoothTransport(client)
        transport.flow_update(None, (2049).to_bytes(4, "big"))
        with self.assertRaises(ConnectionError): await transport.send(Frame(0x40, bytes(56)))
        self.assertFalse(client.writes)
        self.assertFalse(client.is_connected)
    async def test_discovery_uses_advertised_name_not_cached_name(self):
        device = SimpleNamespace(name="FZ Bridge Mico")
        self.assertTrue(bridge_advertisement(device, SimpleNamespace(local_name="FZ Bridge Mico", service_uuids=[DISCOVERY])))
        self.assertFalse(bridge_advertisement(device, SimpleNamespace(local_name="Flipper", service_uuids=[DISCOVERY])))
        self.assertFalse(bridge_advertisement(device, SimpleNamespace(local_name=None, service_uuids=[DISCOVERY])))
    async def test_daemon_prompt_does_not_block_disconnect(self):
        with tempfile.TemporaryDirectory() as directory:
            store = PairingStore(Path(directory) / "pairs.json", Vault())
            value = decode_hello(hello().payload)
            store.establish(value, bytes([1]) * 32)
            prefix = value.client_nonce.to_bytes(8, "little")
            replies = [hello(), Frame(WAITING, prefix + b"\3"), Frame(RESULT, prefix + b"\1")]
            class Peer:
                def __init__(self): self.sent = []
                async def send(self, frame): self.sent.append(frame)
                async def read(self):
                    if replies: return encode_frame(replies.pop(0))
                    raise ConnectionError("unplug")
            peer = Peer(); prompts = []
            def prompt(_text, secret=False):
                future = asyncio.get_running_loop().create_future(); prompts.append(future); return future
            with self.assertRaises(ConnectionError): await serve_demo(peer, store, prompt=prompt)
            self.assertTrue(prompts[0].cancelled())
            self.assertEqual([frame.message_type for frame in peer.sent], [0x40, MessageType.HELLO_ACK, MessageType.PERMISSION_REQUIRED])


if __name__ == "__main__": unittest.main()
