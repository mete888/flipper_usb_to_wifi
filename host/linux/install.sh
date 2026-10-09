#!/bin/sh

set -eu

package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data_root=${XDG_DATA_HOME:-"${HOME:?HOME is required}/.local/share"}
application_root="$data_root/flipper-usb-internet-bridge"
desktop_root="$data_root/applications"
icon_root="$data_root/icons/hicolor/256x256/apps"
desktop_file="$desktop_root/flipper-usb-internet-bridge.desktop"

mkdir -p "$application_root" "$desktop_root" "$icon_root"
install -m 0755 "$package_dir/fib-bridge" "$application_root/fib-bridge"
if [ -d "$package_dir/fib-bridge-desktop" ]; then
    # Keep an old GUI recoverably; never recursively delete a user's XDG root.
    if [ -e "$application_root/fib-bridge-desktop" ]; then
        backup_dir=$(mktemp -d "$application_root/previous-gui.XXXXXX")
        mv "$application_root/fib-bridge-desktop" "$backup_dir/"
        printf 'Previous GUI preserved at: %s\n' "$backup_dir"
    fi
    cp -R "$package_dir/fib-bridge-desktop" "$application_root/"
    launch_path="$application_root/fib-bridge-desktop/fib-bridge-desktop"
    terminal=false
else
    launch_path="$application_root/fib-bridge"
    terminal=true
fi
install -m 0644 "$package_dir/fib-bridge.png" \
    "$icon_root/flipper-usb-internet-bridge.png"

{
    printf '%s\n' '[Desktop Entry]'
    printf '%s\n' 'Type=Application'
    printf '%s\n' 'Version=1.0'
    printf '%s\n' 'Name=Flipper Internet Bridge'
    printf '%s\n' 'Comment=Provide authorized HTTPS access to Flipper Zero apps'
    printf 'TryExec=%s\n' "$launch_path"
    printf 'Exec="%s"\n' "$launch_path"
    printf '%s\n' 'Icon=flipper-usb-internet-bridge'
    printf 'Terminal=%s\n' "$terminal"
    printf '%s\n' 'Categories=Network;Utility;'
} >"$desktop_file"
chmod 0644 "$desktop_file"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$desktop_root" >/dev/null 2>&1 || true
fi

printf 'Installed Flipper Internet Bridge for %s.\n' "${USER:-the current user}"
printf 'Open it from the application menu or run: %s\n' "$launch_path"
