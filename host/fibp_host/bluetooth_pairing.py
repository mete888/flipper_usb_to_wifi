"""BLE-only bridge recognition. No browser/USB grants or OS bonds are used."""
from __future__ import annotations

import hashlib
import hmac
import json
import os
import secrets
import struct
import tempfile
import time
from pathlib import Path

from scripts.fibp_codec import Frame, Hello, MessageType, decode_hello
from .permissions import default_permission_path

OPEN, CODE, NEEDED, RESULT, REVOKE, WAITING, FORGOTTEN = range(0x40, 0x47)
CAPABILITIES = (1 << 5) | (1 << 6)


class PairingError(ValueError):
    pass


class KeyringVault:
    """Fail closed: never accept a plaintext/null/chainer keyring backend."""
    service = "FlipperInternetBridge.Bluetooth.v1"

    def __init__(self):
        import keyring
        self.backend = keyring.get_keyring()
        module = type(self.backend).__module__
        if module not in {"keyring.backends.Windows", "keyring.backends.SecretService",
                          "keyring.backends.kwallet", "keyring.backends.macOS"}:
            raise PairingError("A secure OS keyring is required (Windows Credential Manager / Linux Secret Service or KWallet).")

    def get(self, key):
        value = self.backend.get_password(self.service, key)
        if value is None:
            return None
        try:
            data = bytes.fromhex(value)
        except ValueError as error:
            raise PairingError("Invalid credential in OS keyring") from error
        if len(data) != 80:
            raise PairingError("Invalid credential in OS keyring")
        return data

    def set(self, key, data):
        self.backend.set_password(self.service, key, data.hex())

    def delete(self, key):
        if self.get(key) is not None:
            self.backend.delete_password(self.service, key)


class PairingStore:
    def __init__(self, path: Path | None = None, vault=None):
        self.path = path or default_permission_path().with_name("bluetooth-pairings.json")
        self.vault = vault if vault is not None else KeyringVault()
        if self.path.exists():
            if self.path.stat().st_size > 65536:
                raise PairingError("Pairing metadata is too large")
            self.data = json.loads(self.path.read_text(encoding="utf-8"))
            if (not isinstance(self.data, dict) or self.data.get("schema") != 1 or
                    not isinstance(self.data.get("peers"), dict) or len(self.data["peers"]) > 64):
                raise PairingError("Invalid pairing metadata")
            try:
                host = bytes.fromhex(self.data["host"])
            except (KeyError, TypeError, ValueError) as error:
                raise PairingError("Invalid bridge host identity") from error
            if len(host) != 16:
                raise PairingError("Invalid bridge host identity")
            for key, name in self.data["peers"].items():
                if (not isinstance(key, str) or len(key) != 64 or any(c not in "0123456789abcdef" for c in key) or
                        not isinstance(name, str) or len(name) > 32 or any(not 32 <= ord(c) < 127 for c in name)):
                    raise PairingError("Invalid pairing metadata")
        else:
            self.data = {"schema": 1, "host": secrets.token_bytes(16).hex(), "peers": {}}
            self._save()
        self.host_id = bytes.fromhex(self.data["host"])

    @staticmethod
    def key(hello: Hello):
        return hashlib.sha256(b"fib-ble-peer-v1\0" + bytes([hello.id_type]) + hello.device_id).hexdigest()

    def _save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary = tempfile.mkstemp(prefix="ble-pair-", dir=self.path.parent)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(self.data, stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)

    def token(self, hello):
        key = self.key(hello)
        if key not in self.data["peers"]:
            return None
        credential = self.vault.get(key)
        expected = self.host_id + bytes.fromhex(key)
        if not credential or len(credential) != 80 or not hmac.compare_digest(credential[:48], expected):
            return None
        return credential[48:]

    def establish(self, hello, token):
        if len(token) != 32 or not any(token):
            raise PairingError("Invalid recognition token")
        key = self.key(hello)
        if key not in self.data["peers"] and len(self.data["peers"]) >= 64:
            raise PairingError("Remove an old pairing before adding another")
        # Persist secret first; roll back the in-memory row if metadata fails.
        # An orphan secret is not trusted without its durable metadata row.
        self.vault.set(key, self.host_id + bytes.fromhex(key) + token)
        name = "".join(c if 32 <= ord(c) < 127 else "_" for c in hello.name)[:32]
        previous = self.data["peers"].get(key)
        self.data["peers"][key] = name or "Flipper Zero"
        try:
            self._save()
        except Exception:
            if previous is None:
                self.data["peers"].pop(key, None)
            else:
                self.data["peers"][key] = previous
            raise

    def revoke(self, key):
        if key not in self.data["peers"]:
            return
        self.vault.delete(key)  # Never pretend success if secure deletion fails.
        del self.data["peers"][key]
        self._save()

    def records(self):
        return sorted(self.data["peers"].items())


class PairingClient:
    def __init__(self, store: PairingStore, clock=time.monotonic):
        self.store, self.clock = store, clock
        self.hello = None
        self.accepted = self.waiting = self.selection_seen = self.awaiting_code = False
        self.awaiting_reply = False
        self.code_started = False
        self.pending_token = None
        self.deadline = 0.0

    def prefix(self):
        return struct.pack("<Q", self.hello.client_nonce) + self.store.host_id

    @staticmethod
    def valid(frame):
        return (frame.major, frame.minor, frame.request_id, frame.sequence, frame.flags) == (1, 0, 0, 0, 0)

    def begin(self, frame):
        if self.hello or frame.message_type != MessageType.HELLO or not self.valid(frame):
            raise PairingError("Invalid Bluetooth HELLO")
        hello = decode_hello(frame.payload)
        if (hello.capabilities & CAPABILITIES != CAPABILITIES or
                not hello.minimum_major <= 1 <= hello.maximum_major or
                (hello.minimum_major == 1 and hello.minimum_minor > 0) or
                not hello.model.lower().startswith("flipper") or hello.id_type != 1 or not hello.device_id):
            raise PairingError("Update both the FAP and helper for Bluetooth Demo")
        self.hello = hello
        self.deadline = self.clock() + 120
        self.awaiting_reply = True
        return Frame(OPEN, self.prefix() + (self.store.token(hello) or bytes(32)))

    def forgotten(self, frame):
        return (self.hello is not None and self.valid(frame) and frame.message_type == FORGOTTEN and
                hmac.compare_digest(frame.payload, self.prefix()))

    def receive(self, frame):
        if self.clock() >= self.deadline:
            raise PairingError("Pairing request/code expired; enable Demo again")
        if (not self.hello or self.accepted or not self.awaiting_reply or not self.valid(frame) or
                len(frame.payload) != 9 or frame.payload[:8] != self.prefix()[:8]):
            raise PairingError("Invalid pairing reply")
        status = frame.payload[8]
        if frame.message_type == WAITING and status == 3 and not self.selection_seen:
            self.selection_seen = self.waiting = True
            return "waiting"
        if not self.selection_seen:
            raise PairingError("Physical selection on Flipper is required")
        if frame.message_type == NEEDED and status == 2:
            if not self.code_started:
                self.store.revoke(self.store.key(self.hello))
                self.deadline = self.clock() + 120
                self.code_started = True
            self.waiting = self.awaiting_reply = False
            self.awaiting_code = True
            return "code"
        if frame.message_type != RESULT or status != 1:
            raise PairingError("Pairing rejected; enable Demo again")
        if self.pending_token is not None:
            self.store.establish(self.hello, self.pending_token)
        elif self.store.token(self.hello) is None:
            raise PairingError("Unknown peer cannot be accepted without code")
        self.accepted = True
        self.waiting = self.awaiting_reply = False
        self.pending_token = None
        return "accepted"

    def submit(self, digits):
        if self.clock() >= self.deadline or not self.awaiting_code or len(digits) != 6 or any(c not in "0123456789" for c in digits):
            raise PairingError("Enter six ASCII digits while the code is valid")
        self.pending_token = secrets.token_bytes(32)
        self.awaiting_code = False
        self.awaiting_reply = True
        return Frame(CODE, self.prefix() + digits.encode("ascii") + self.pending_token)
