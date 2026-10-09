"""Build a native-platform GUI directory plus the existing terminal companion.

Run on Windows or Linux; PyInstaller is not a cross-compiler. No source files or
credential stores are edited. Artifacts are generated below dist/host-desktop.
"""
from __future__ import annotations

import argparse
import importlib.metadata
import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def copy_notices(target):
    licenses = target / "licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    inventory = []
    for distribution in importlib.metadata.distributions():
        name = distribution.metadata["Name"]
        inventory.append({"name": name, "version": distribution.version,
                          "license": distribution.metadata.get("License-Expression") or distribution.metadata.get("License", "See upstream")})
        for entry in distribution.files or ():
            if not any(word in entry.name.lower() for word in ("license", "copying", "notice")):
                continue
            source = Path(distribution.locate_file(entry))
            if source.is_file() and source.suffix.lower() not in {".py", ".pyc", ".so", ".dll", ".pyd"}:
                destination = licenses / name / str(entry).replace("../", "")
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
    (target / "dependency-versions.json").write_text(json.dumps(sorted(inventory, key=lambda item: item["name"].lower()), indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("windows-x86_64", "linux-x86_64"), required=True)
    args = parser.parse_args()
    expected = "Windows" if args.platform.startswith("windows") else "Linux"
    if platform.system() != expected or platform.machine().lower() not in {"amd64", "x86_64"}:
        parser.error("Build on the target x86_64 operating system; this is not cross-compilation")
    root = Path(__file__).resolve().parents[1]
    output = root / "dist" / "host-desktop"
    output.mkdir(parents=True, exist_ok=True)
    build = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--windowed",
             "--collect-all", "bleak", "--collect-all", "keyring", "--collect-data", "host",
             "--name", "fib-bridge-desktop", "--distpath", str(output),
             "--workpath", str(root / ".build-tests" / "desktop-package"),
             "--specpath", str(root / ".build-tests" / "desktop-package")]
    if expected == "Windows": build += ["--icon", str(root / "host/assets/fib-bridge.ico")]
    subprocess.run(build + [str(root / "host/fib_bridge_desktop_entry.py")], cwd=root, check=True)
    bundle = output / "fib-bridge-desktop"
    for source in (root / "LICENSE", root / "host/THIRD_PARTY_DESKTOP.md", root / "host/README.md"):
        shutil.copy2(source, bundle / source.name)
    copy_notices(bundle)
    if expected == "Windows":
        archive = shutil.make_archive(str(output / "fib-bridge-desktop-windows-x86_64"), "zip", output, bundle.name)
    else:
        package = output / "fib-bridge-linux-x86_64-package"
        package.mkdir(exist_ok=True)
        shutil.copytree(bundle, package / bundle.name, dirs_exist_ok=True)
        shutil.copy2(root / "dist/fib-bridge-linux-x86_64", package / "fib-bridge")
        for name in ("install.sh", "uninstall.sh"):
            shutil.copy2(root / "host/linux" / name, package / name)
        shutil.copy2(root / "host/assets/fib-bridge.png", package / "fib-bridge.png")
        shutil.copy2(root / "host/README.md", package / "README.md")
        archive = shutil.make_archive(str(output / "fib-bridge-linux-x86_64"), "gztar", output, package.name)
    print(archive)


if __name__ == "__main__":
    main()
