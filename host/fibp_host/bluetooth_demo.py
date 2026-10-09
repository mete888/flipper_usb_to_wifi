"""Terminal Demo/Pairings interface; USB stays the default host mode."""
from __future__ import annotations
import asyncio
import getpass
import queue
import threading
import time

from scripts.fibp_codec import MessageType, StreamDecoder
from .bluetooth_pairing import PairingClient, PairingStore
from .bluetooth_transport import connect_demo
from .permissions import PermissionStore
from .session import PermissionDecision


def prompt_line(prompt, secret=False):
    """Daemon input: disconnect/timeout cannot hang event-loop shutdown on stdin."""
    loop = asyncio.get_running_loop()
    future = loop.create_future()

    def finish(value, error):
        if not future.done():
            if error:
                future.set_exception(error)
            else:
                future.set_result(value)

    def read():
        try:
            value, error = (getpass.getpass(prompt) if secret else input(prompt)), None
        except (EOFError, KeyboardInterrupt) as caught:
            value, error = None, caught
        try:
            loop.call_soon_threadsafe(finish, value, error)
        except RuntimeError:
            pass  # Session already expired/disconnected; stale input is ignored.

    threading.Thread(target=read, daemon=True, name="ble-consent-input").start()
    return future


async def serve_demo(transport, store, verbose=False, prompt=prompt_line,
                     host_factory=None, on_update=None, on_tick=None, stop=None):
    from .cli import BridgeHost
    host = host_factory() if host_factory is not None else BridgeHost(PermissionStore(), None, verbose, bluetooth=True)
    pairing = PairingClient(store)
    pending_input = None
    input_kind = None
    initial_hello = None
    started = last_activity = time.monotonic()
    frame_started = None

    def parser_error(issue):
        raise RuntimeError(f"Invalid BLE frame: {issue.code}")

    decoder = StreamDecoder(parser_error)
    try:
        while stop is None or not stop.is_set():
            if on_update is not None:
                on_update(host, pairing)
            if on_tick is not None:
                for reply in on_tick(host, pairing):
                    await transport.send(reply)
            if not pairing.accepted and time.monotonic() >= (pairing.deadline if pairing.hello else started + 120):
                raise TimeoutError("Bluetooth selection/code timed out")
            if time.monotonic() - last_activity > 120:
                raise TimeoutError("Bluetooth idle timeout")
            if pending_input is not None and pending_input.done():
                answer = pending_input.result().strip()
                pending_input = None
                if input_kind == "code":
                    if answer.lower() in {"cancel", ""}:
                        return
                    try:
                        packet = pairing.submit(answer)
                    except ValueError:
                        pending_input = prompt("Six-digit bridge code (empty cancels): ", secret=True)
                    else:
                        await transport.send(packet)
                else:
                    if answer.lower() not in {"o", "once", "d", "deny"}:
                        pending_input = prompt("[d] Deny  [o] Allow once: ")
                    else:
                        choice = PermissionDecision.ALLOW_ONCE if answer.lower() in {"o", "once"} else PermissionDecision.DENY
                        for reply in host.session.resolve_permission(choice):
                            await transport.send(reply)
            data = await transport.read()
            if data:
                last_activity = time.monotonic()
                for frame in decoder.feed(data):
                    if pairing.forgotten(frame):
                        store.revoke(store.key(pairing.hello))
                        print("Pairing revoked by Flipper. Internet stopped.", flush=True)
                        return
                    if not pairing.accepted:
                        if pairing.hello is None:
                            initial_hello = frame
                            await transport.send(pairing.begin(frame))
                            if on_update is not None: on_update(host, pairing)
                            continue
                        state = pairing.receive(frame)
                        if on_update is not None: on_update(host, pairing)
                        if state == "waiting":
                            print(f"On Flipper: Bluetooth Internet Bridge > Connection Requests > Bridge PC - {store.host_id[:2].hex().upper()} > Pair/Connect.", flush=True)
                        elif state == "code":
                            input_kind = "code"
                            pending_input = prompt("Six-digit bridge code (empty cancels): ", secret=True)
                        else:
                            print("Recognized by bridge; internet access is NOT yet allowed.", flush=True)
                            for reply in host.session.on_frame(initial_hello):
                                await transport.send(reply)
                            input_kind = "permission"
                            print("This Flipper wants to use this computer's internet. Wi-Fi password/cookies are not shared.", flush=True)
                            pending_input = prompt("[d] Deny  [o] Allow once: ")
                    else:
                        if frame.message_type == MessageType.DISCONNECT:
                            return
                        if frame.message_type == MessageType.HELLO:
                            raise RuntimeError("Unexpected HELLO after recognition; reconnect Bluetooth")
                        for reply in host.session.on_frame(frame):
                            await transport.send(reply)
                # A partial packet has a bounded lifetime even if bytes trickle.
                if decoder.buffered_bytes:
                    if frame_started is None:
                        frame_started = time.monotonic()
                else:
                    frame_started = None
            if frame_started is not None and time.monotonic() - frame_started > 6:
                raise TimeoutError("Incomplete Bluetooth frame timed out")
            # Drain a small batch, then check peer/CANCEL again. A live network
            # producer must not starve incoming control traffic indefinitely.
            for _ in range(4):
                try:
                    reply = host.outgoing.get_nowait()
                except queue.Empty:
                    break
                await transport.send(reply)
    finally:
        if pending_input is not None:
            pending_input.cancel()
        host.close()  # Cancels active network work on every exit/disconnect.


def main_demo(args):
    try:
        store = PairingStore()
        if args.demo:
            print("Bluetooth Internet Bridge\n1. Connect Bluetooth\n2. Pairings\n3. Revoke Pairing\n4. Exit")
            choice = input("Select: ").strip()
            if choice == "4":
                return 0
            if choice not in {"1", "2", "3"}:
                return 2
            args.bluetooth_demo, args.pairings = choice == "1", choice in {"2", "3"}
            if choice == "3":
                records = store.records()
                for index, (_key, name) in enumerate(records, 1):
                    print(f"{index}. {name}")
                selected = input("Number to revoke (empty cancels): ").strip()
                if not selected:
                    return 0
                if not selected.isascii() or not selected.isdigit() or not 1 <= int(selected) <= len(records):
                    return 2
                args.revoke_pairing = records[int(selected) - 1][0]
        if args.revoke_pairing:
            if args.revoke_pairing not in dict(store.records()):
                raise ValueError("Use the exact ID shown by --pairings")
            if input("Revoke bridge pairing? [y/N]: ").strip().lower() != "y":
                return 0
            store.revoke(args.revoke_pairing)
            print("Pairing removed. The next connection needs Pair and a new code; OS bonds/USB grants unchanged.")
            return 0
        if args.pairings:
            print("Flippers recognized by this bridge (not OS Bluetooth bonds)")
            for key, name in store.records():
                print(f"{key}  {name}")
            if not store.records():
                print("No recognized Flippers")
            return 0
        asyncio.run(connect_demo(lambda transport: serve_demo(transport, store, args.verbose)))
        return 0
    except KeyboardInterrupt:
        return 130
    except ImportError:
        print('Bluetooth Internet Bridge requires: python -m pip install ".[bluetooth]"')
        return 1
    except Exception as error:
        print(f"Bluetooth Internet Bridge stopped: {error}")
        return 1
