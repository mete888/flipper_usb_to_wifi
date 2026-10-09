# FIBP hardware-free simulator

## Live provider smoke checks

From the repository root, use the portable helper's real HTTPS client without
opening a serial port or changing device permissions:

```sh
python3 -m scripts.live_endpoint_smoke --live
```

This requires the existing host environment/dependencies. The explicit `--live`
flag opts into 16 bounded public-provider requests; any failed check returns a
nonzero exit status. Ordinary unit tests remain offline. See the
[latest local QA report](../docs/test-results-2026-10-04.md) for real-device
results, failures and validation limits.

`build_sdk_example.sh` stages and builds the standalone SDK consumer without
overwriting the main `application.fam`. It tests the one-command export and
two wildcard source patterns documented for consumers:

```sh
UFBT=/path/to/ufbt ./scripts/build_sdk_example.sh
```

Copy just the reusable SDK into another FAP (no Python packages required):

```sh
python3 scripts/vendor_bridge_sdk.py --destination /path/to/your_fap/vendor/internet_bridge
```

Existing destinations are refused, not overwritten. See the
[SDK quick start](../sdk/flipper/README.md) for source patterns and the ready-made
USB/Bluetooth connection screen.

`fibp_simulator.py` creates a raw pseudo-terminal, prints its slave path on
stdout, and emulates one FIBP endpoint. It does not make a real network request.
All diagnostics are written to stderr.

Run a simulated Flipper for a desktop helper that accepts a manual serial path:

```sh
python3 scripts/fibp_simulator.py \
  --role flipper \
  --fragment-sizes 1,3,7,64
```

Run a simulated Mac peer for a Flipper-side protocol client:

```sh
python3 scripts/fibp_simulator.py \
  --role mac \
  --fragment-sizes 2,5,64
```

The first stdout line is a path such as `/dev/ttys003`. Open that path in the
component under test using raw serial settings.

Corrupt the first outgoing PING frame's trailer CRC:

```sh
python3 scripts/fibp_simulator.py \
  --role flipper \
  --corrupt-crc frame \
  --corrupt-message PING \
  --fragment-sizes 1,2,3
```

Run the hardware-free test suite:

```sh
python3 -m unittest -v tests.test_fibp_codec tests.test_fibp_simulator
```
