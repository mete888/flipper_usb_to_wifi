"""Capture the real Flipper display on primary CDC while the bridge uses CDC 2.

Operator-only QA utility; never grants permissions or changes firmware.
Wire field numbers come from the official gui.proto / flipper.proto:
https://github.com/flipperdevices/flipperzero-protobuf
Requires the project's existing pyserial dependency; PNG uses the stdlib.
"""
import argparse
import struct
import time
import zlib
from pathlib import Path

import serial


def varint(value):
    out = bytearray()
    while value > 127:
        out.append((value & 127) | 128); value >>= 7
    out.append(value)
    return bytes(out)


def decode_varint(data, offset):
    value = 0
    for shift in range(0, 35, 7):
        if offset >= len(data): return None
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if byte < 128: return value, offset
    raise ValueError("Invalid RPC varint")


def fields(data):
    offset = 0
    while offset < len(data):
        tag, offset = decode_varint(data, offset)
        wire = tag & 7
        if wire == 0:
            value, offset = decode_varint(data, offset)
        elif wire == 2:
            length, offset = decode_varint(data, offset)
            if offset + length > len(data): raise ValueError("Truncated RPC field")
            value = data[offset:offset + length]; offset += length
        elif wire in (1, 5):
            length = 8 if wire == 1 else 4
            value = data[offset:offset + length]; offset += length
        else: raise ValueError("Unsupported RPC wire type")
        yield tag >> 3, value


def message(command, field, payload=b""):
    body = b"\x08" + varint(command) + varint((field << 3) | 2) + varint(len(payload)) + payload
    return varint(len(body)) + body


def capture(port, keys, seconds):
    latest = None
    buffer = bytearray()
    with serial.Serial(port, 115200, timeout=0.05, write_timeout=1) as device:
        # Opening CDC asserts DTR and the firmware starts CLI asynchronously.
        # Sending a command before its prompt can be dropped on slower builds.
        greeting = bytearray()
        deadline = time.monotonic() + 5
        while b">: " not in greeting:
            greeting.extend(device.read(4096))
            if len(greeting) > 8192 or time.monotonic() >= deadline:
                raise RuntimeError("Primary CDC did not present a CLI prompt")
        device.write(b"start_rpc_session\r")
        # Discard CLI banner / echo before beginning length-prefixed protobuf.
        time.sleep(0.2)
        device.reset_input_buffer()
        device.write(message(1, 20))
        command = 2
        key_numbers = {"up": 0, "down": 1, "right": 2, "left": 3, "ok": 4, "back": 5}
        for key in keys:
            # rpc_gui.c clears the per-key sequence on RELEASE. SHORT must
            # therefore precede RELEASE so all three share the same sequence.
            for kind in (0, 2, 1):  # PRESS, SHORT, RELEASE
                device.write(message(command, 23, b"\x08" + varint(key_numbers[key]) + b"\x10" + varint(kind)))
                command += 1
            time.sleep(0.08)
        deadline = time.monotonic() + seconds
        try:
            while time.monotonic() < deadline:
                buffer.extend(device.read(4096))
                while buffer:
                    decoded = decode_varint(buffer, 0)
                    if decoded is None: break
                    size, start = decoded
                    if size > 32768: raise ValueError("Oversized RPC message / CLI not in RPC mode")
                    if len(buffer) < start + size: break
                    packet = bytes(buffer[start:start + size]); del buffer[:start + size]
                    decoded_fields = dict(fields(packet))
                    if decoded_fields.get(2, 0):
                        raise RuntimeError(f"RPC command {decoded_fields.get(1)} status {decoded_fields[2]}")
                    for field, value in decoded_fields.items():
                        if field == 22:
                            frame = dict(fields(value))
                            if frame.get(2, 0) != 0: raise ValueError("Only horizontal display supported")
                            if len(frame.get(1, b"")) == 1024: latest = frame[1]
        finally:
            device.write(message(command, 21))
            device.write(message(command + 1, 19))
            # Do not call tcdrain/flush: macOS CDC can block indefinitely there.
            # Drain stop acknowledgements before dropping DTR.
            time.sleep(0.1)
            device.read(4096)
    if latest is None: raise RuntimeError("No real screen frame received")
    return latest


def png(frame, scale=1):
    if scale not in range(1, 9): raise ValueError("Use an integer display scale from 1 to 8")
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    # u8g2 screen buffer is vertical 8-pixel tiles, 128 columns per tile row.
    rows = bytearray()
    for y in range(64 * scale):
        rows.append(0)
        for x in range(128 * scale):
            pixel_y, pixel_x = y // scale, x // scale
            rows.append(0 if frame[(pixel_y // 8) * 128 + pixel_x] & (1 << (pixel_y % 8)) else 255)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 128 * scale, 64 * scale, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Primary CLI CDC, NOT the bridge/helper CDC")
    parser.add_argument("--keys", default="", help="Comma-separated up,down,left,right,ok,back")
    parser.add_argument("--seconds", type=float, default=2.0)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scale", type=int, choices=range(1, 9), default=1, help="Integer nearest-pixel scale; no smoothing or generated pixels")
    args = parser.parse_args()
    keys = args.keys.split(",") if args.keys else []
    if any(key not in {"up", "down", "left", "right", "ok", "back"} for key in keys):
        parser.error("Invalid key")
    if not 0.1 <= args.seconds <= 15: parser.error("Use 0.1-15 capture seconds")
    result = capture(args.port, keys, args.seconds)
    args.output.write_bytes(png(result, args.scale))
    print(f"Real Flipper framebuffer: {args.output}")
