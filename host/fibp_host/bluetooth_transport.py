"""Opt-in BLE peripheral client for Windows/Linux (Bleak, no serial emulation)."""
from __future__ import annotations
import asyncio
import sys

from scripts.fibp_codec import encode_frame

DISCOVERY = "00003080-0000-1000-8000-00805f9b34fb"
SERVICE = "8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000"
TX = "19ed82ae-ed21-4c9d-4145-228e61fe0000"
RX = "19ed82ae-ed21-4c9d-4145-228e62fe0000"
FLOW = "19ed82ae-ed21-4c9d-4145-228e63fe0000"
STATUS = "19ed82ae-ed21-4c9d-4145-228e64fe0000"


def bridge_advertisement(_device, advertisement):
    name = advertisement.local_name
    services = {value.lower() for value in advertisement.service_uuids}
    return (DISCOVERY in services and isinstance(name, str) and name.startswith("FZ Bridge ") and
            11 <= len(name) <= 16 and all(32 <= ord(c) < 127 for c in name))


class BluetoothTransport:
    def __init__(self, client):
        self.client = client
        self.incoming = asyncio.Queue(maxsize=64)
        self.closed = asyncio.Event()
        self.credit_changed = asyncio.Event()
        self.credits = 0
        self.failure = None
        self.write_lock = asyncio.Lock()
        self.write_size = 20

    def fail(self, error):
        self.failure = error
        self.closed.set()
        self.credit_changed.set()

    def receive(self, _characteristic, data):
        if self.closed.is_set():
            return
        if not 0 < len(data) <= 512:
            self.fail(RuntimeError("Invalid BLE notification length"))
            return
        try:
            self.incoming.put_nowait(bytes(data))
        except asyncio.QueueFull:
            self.fail(RuntimeError("BLE receive queue overflow; disconnected"))

    def flow_update(self, _characteristic, data):
        if len(data) != 4 or int.from_bytes(data, "big") > 2048:
            self.fail(RuntimeError("Invalid BLE receive credits"))
            return
        self.credits = int.from_bytes(data, "big")
        self.credit_changed.set()

    async def start(self):
        service = self.client.services.get_service(SERVICE)
        if not service:
            raise RuntimeError("Bridge GATT service not found")
        required = {TX: "indicate", RX: "write", FLOW: "notify", STATUS: "write"}
        for uuid, prop in required.items():
            char = service.get_characteristic(uuid)
            if char is None or prop not in char.properties:
                raise RuntimeError("Bridge GATT characteristics are incomplete")
        # Acknowledged writes use negotiated MTU, never assume a 244-byte link.
        # BlueZ may conservatively report 23; that keeps the safe 20-byte fallback.
        rx_characteristic = service.get_characteristic(RX)
        self.write_size = max(20, min(128, max(int(self.client.mtu_size) - 3,
            rx_characteristic.max_write_without_response_size)))
        await self.client.start_notify(TX, self.receive)
        await self.client.start_notify(FLOW, self.flow_update)
        # The firmware requires authenticated/encrypted access to these fields.
        # Win/Linux pair=True uses the native OS PIN, never weaker GATT security.
        self.flow_update(None, await self.client.read_gatt_char(FLOW))
        if self.failure:
            raise self.failure
        await self.client.write_gatt_char(STATUS, bytes(4), response=True)

    async def read(self):
        if self.closed.is_set() or not self.client.is_connected:
            raise self.failure or ConnectionError("Bluetooth disconnected")
        try:
            return await asyncio.wait_for(self.incoming.get(), 0.05)
        except asyncio.TimeoutError:
            return b""

    async def send(self, frame):
        async with self.write_lock:
            try:
                await asyncio.wait_for(self._send(encode_frame(frame)), 4)
            except BaseException:
                self.fail(ConnectionError("Bluetooth frame delivery failed"))
                # Never reuse an uncertain partial frame on the same link.
                await self.client.disconnect()
                raise

    async def _send(self, data):
        for offset in range(0, len(data), self.write_size):
            chunk = data[offset:offset + self.write_size]
            while self.credits < len(chunk):
                self.credit_changed.clear()
                if self.closed.is_set():
                    raise ConnectionError("Bluetooth disconnected")
                await self.credit_changed.wait()
            if self.closed.is_set() or not self.client.is_connected:
                raise ConnectionError("Bluetooth disconnected")
            self.credits -= len(chunk)
            await self.client.write_gatt_char(RX, chunk, response=True)


async def connect_demo(serve):
    from bleak import BleakClient, BleakScanner
    print("Searching for FZ Bridge (select Bluetooth Internet Bridge on Flipper).", flush=True)
    device = await BleakScanner.find_device_by_filter(bridge_advertisement, timeout=60)
    if device is None:
        raise RuntimeError("No Flipper Bridge advertisement found")
    transport = None

    def disconnected(_client):
        if transport is not None:
            transport.fail(ConnectionError("Bluetooth disconnected"))

    # Native pairing is automatic for authenticated characteristics on macOS.
    async with BleakClient(device, disconnected_callback=disconnected,
                           pair=sys.platform != "darwin", timeout=90) as client:
        transport = BluetoothTransport(client)
        await asyncio.wait_for(transport.start(), 90)
        await serve(transport)
