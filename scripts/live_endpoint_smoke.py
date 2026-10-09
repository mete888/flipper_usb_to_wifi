"""Opt-in, bounded public-provider checks using the real portable HTTPS client.

Does not open a serial port, approve a device, or alter persistent permissions.
Run from the repository root: python3 -m scripts.live_endpoint_smoke --live
Provider failures are reported, never converted to skipped/passing tests.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
from threading import Event

from host.fibp_host.network import perform_request
from scripts.fibp_codec import RequestStart


CASES = (
    ("sample", "https://api.github.com/zen", "text"),
    ("time", "https://postman-echo.com/time/now", "text"),
    ("wikipedia", "https://en.wikipedia.org/w/api.php?action=query&generator=search&gsrlimit=1&prop=extracts&exchars=420&explaintext=1&redirects=1&format=json&formatversion=2&gsrsearch=Flipper%20Zero", "json"),
    ("weather-search", "https://geocoding-api.open-meteo.com/v1/search?count=3&language=en&format=json&name=London", "json"),
    ("weather", "https://api.open-meteo.com/v1/forecast?latitude=51.5&longitude=-0.12&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=1", "json"),
    ("national-today", "https://nationaltoday.com/today/", "text"),
    ("iss", "https://api.wheretheiss.at/v1/satellites/25544", "json"),
    ("radio-directory", "https://all.api.radio-browser.info/json/stations/search?limit=5&hidebroken=true&is_https=true&codec=MP3&bitrateMax=64&order=clickcount&reverse=true&countrycode=GB", "radio"),
    ("btc", "https://data-api.binance.vision/api/v3/ticker/24hr?symbol=BTCUSDT", "json"),
    ("eth", "https://data-api.binance.vision/api/v3/ticker/24hr?symbol=ETHUSDT", "json"),
    ("gold", "https://api.gold-api.com/price/XAU", "json"),
    ("brent", "https://fapi.binance.com/fapi/v1/ticker/24hr?symbol=BZUSDT", "json"),
    ("silver", "https://api.gold-api.com/price/XAG", "json"),
    ("earthquakes", "https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&orderby=time&limit=10&eventtype=earthquake", "FIBTOOLS1"),
    ("currency", "https://api.frankfurter.dev/v2/rate/USD/EUR", "FIBRATE1"),
    ("dictionary", "https://api.dictionaryapi.dev/api/v2/entries/en/hello", "FIBTOOLS1"),
)


def check(case: tuple[str, str, str]) -> dict:
    name, url, expected = case
    status = 0
    chunks = []

    def response(code, _headers, _length):
        nonlocal status
        status = code

    try:
        result = perform_request(RequestStart(1, 15000, url), [], b"", 4096,
                                 Event(), response, chunks.append)
        data = b"".join(chunks)
        if status != 200 or result.cancelled or result.truncated or not data:
            raise ValueError(f"HTTP {status}; truncated={result.truncated}; cancelled={result.cancelled}")
        if expected == "json":
            json.loads(data)
        elif expected == "radio":
            if not data.startswith(b"FIBRADIO1\n") or len(data.splitlines()) < 2:
                raise ValueError("Directory returned no compatible HTTPS MP3 stations")
        elif expected != "text" and not data.startswith(expected.encode() + b"\n"):
            raise ValueError("Unexpected compact payload format")
        return {"name": name, "result": "PASS", "http": status,
                "bytes": len(data), "chunks": len(chunks)}
    except Exception as error:
        # No response contents or user-supplied queries in the report.
        return {"name": name, "result": "FAIL", "http": status,
                "error": f"{type(error).__name__}: {error}"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live", action="store_true", help="send public HTTPS test requests")
    args = parser.parse_args()
    if not args.live:
        parser.error("Pass --live explicitly; ordinary tests stay offline")
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(check, CASES))
    for result in results:
        print(json.dumps(result, sort_keys=True), flush=True)
    return 1 if any(result["result"] != "PASS" for result in results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
