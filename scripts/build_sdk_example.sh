#!/bin/sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ufbt_command=${UFBT:-ufbt}
# Retain staging for inspection; never replace the main application's manifest.
stage=$(mktemp -d "${TMPDIR:-/tmp}/fib-sdk-example.XXXXXX")
python3 "$project_root/scripts/vendor_bridge_sdk.py" --destination "$stage/vendor/internet_bridge"
mkdir -p "$stage/vendor/internet_bridge/examples/flipper_bridge_client"
cp "$project_root/examples/flipper_bridge_client/example_app.c" "$stage/vendor/internet_bridge/examples/flipper_bridge_client/"
cp "$project_root/examples/flipper_bridge_client/application.fam" "$stage/application.fam"
(cd "$stage" && "$ufbt_command")
printf 'SDK example FAP: %s/dist/fib_sdk_example.fap\n' "$stage"
