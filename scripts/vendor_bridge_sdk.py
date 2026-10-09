#!/usr/bin/env python3
"""Copy the complete source SDK into a consumer FAP, without dependencies.

Existing destinations are never overwritten. Export into a new destination
when updating, compare it and replace the previous vendor copy deliberately.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import tempfile

ROOT_FILES = (
    "bridge_session.c", "bridge_session.h", "bridge_timing.h",
    "bridge_protocol.c", "bridge_protocol.h", "usb_transport.c", "usb_transport.h",
    "ble_transport.c", "ble_transport.h", "ble_discovery_config.h",
    "ble_pairing.c", "ble_pairing.h", "ble_pairing_storage.c", "ble_pairing_storage.h",
    "config.h", "LICENSE",
)
SDK_FILES = ("fib_bridge_client.c", "fib_bridge_client.h", "fib_bridge_setup.c", "fib_bridge_setup.h", "README.md")


def export_sdk(root: Path, destination: Path) -> None:
    if destination.is_symlink():
        raise ValueError("Destination is a symlink; no files were overwritten")
    destination = destination.resolve()
    if destination.exists():
        raise ValueError("Destination already exists; choose a new directory (no files were overwritten)")
    for relative in (*ROOT_FILES, *("sdk/flipper/" + name for name in SDK_FILES)):
        if not (root / relative).is_file():
            raise ValueError(f"Required SDK source missing: {relative}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".fib-sdk-export-", dir=destination.parent))
    try:
        (stage / "sdk/flipper").mkdir(parents=True)
        for name in ROOT_FILES:
            shutil.copy2(root / name, stage / name)
        for name in SDK_FILES:
            shutil.copy2(root / "sdk/flipper" / name, stage / "sdk/flipper" / name)
        # copytree atomically creates the destination directory with exist_ok=False.
        # Unlike POSIX rename, it cannot replace an existing empty directory.
        shutil.copytree(stage, destination)
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=Path)
    args = parser.parse_args()
    try:
        export_sdk(Path(__file__).resolve().parents[1], args.destination)
    except (ValueError, OSError) as error:
        parser.exit(1, f"SDK export failed: {error}\n")
    print(f"SDK exported: {args.destination.resolve()}")
    print('Add your vendor directory + "/*.c" and "/sdk/flipper/*.c" to application.fam sources.')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
