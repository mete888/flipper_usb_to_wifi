"""Bounded, ASCII-only display payloads for the Toolbox endpoints."""
from __future__ import annotations

import json
import math
import re
import unicodedata
from datetime import datetime, timezone
from urllib.parse import urlsplit

MAXIMUM_SOURCE_BYTES = 64 * 1024
MAXIMUM_TEXT_BYTES = 1400
MAXIMUM_EARTHQUAKES = 10
EARTHQUAKES = "earthquakes"
DICTIONARY = "dictionary"
CURRENCY = "currency"


def kind_for(url: str) -> str | None:
    parts = urlsplit(url)
    if parts.scheme.lower() != "https":
        return None
    host = (parts.hostname or "").lower()
    if host == "earthquake.usgs.gov" and parts.path == "/fdsnws/event/1/query":
        return EARTHQUAKES
    if host == "api.dictionaryapi.dev" and re.fullmatch(r"/api/v2/entries/en/[a-zA-Z%'-]+", parts.path):
        return DICTIONARY
    if host == "api.frankfurter.dev" and re.fullmatch(r"/v2/rate/[A-Z]{3}/[A-Z]{3}", parts.path):
        return CURRENCY
    return None


def _text(value: object, limit: int) -> str:
    if not isinstance(value, str):
        return ""
    for original, replacement in (("\u2014", "-"), ("\u2013", "-"), ("\u2019", "'"), ("\u2018", "'"), ("\u201c", '"'), ("\u201d", '"')):
        value = value.replace(original, replacement)
    ascii_text = unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii")
    # Screen formatter escape codes must never come from remote content.
    ascii_text = "".join(c if 32 <= ord(c) <= 126 else " " for c in ascii_text)
    return " ".join(ascii_text.split())[:limit]


def _number(value: object) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    try:
        number = float(value)
    except (OverflowError, ValueError):
        return None
    return number if math.isfinite(number) else None


def _encoded(text: str) -> bytes:
    result = text.encode("ascii")
    if len(result) > MAXIMUM_TEXT_BYTES:
        raise ValueError("Toolbox display is too large")
    return result


def extract(kind: str, source: bytes) -> bytes:
    if len(source) > MAXIMUM_SOURCE_BYTES:
        raise ValueError("Toolbox source is too large")
    data = json.loads(source)
    if kind == CURRENCY:
        if not isinstance(data, dict):
            raise ValueError("Currency response is not an object")
        base, quote, date = data.get("base"), data.get("quote"), data.get("date")
        rate = _number(data.get("rate"))
        if not isinstance(base, str) or not re.fullmatch(r"[A-Z]{3}", base) or not isinstance(quote, str) or not re.fullmatch(r"[A-Z]{3}", quote):
            raise ValueError("Invalid currency pair")
        if not isinstance(date, str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", date) or rate is None or not 0 < rate <= 1e9:
            raise ValueError("Invalid currency rate")
        datetime.strptime(date, "%Y-%m-%d")
        return _encoded(f"FIBRATE1\n{base}\t{quote}\t{rate:.12f}\t{date}\n")
    if kind == EARTHQUAKES:
        if not isinstance(data, dict) or not isinstance(data.get("features"), list):
            raise ValueError("Invalid earthquake collection")
        events = []
        for feature in data["features"]:
            if not isinstance(feature, dict):
                continue
            p, g = feature.get("properties"), feature.get("geometry")
            if not isinstance(p, dict) or not isinstance(g, dict) or p.get("type") != "earthquake":
                continue
            stamp, magnitude = _number(p.get("time")), _number(p.get("mag"))
            coordinates = g.get("coordinates")
            depth = _number(coordinates[2]) if isinstance(coordinates, list) and len(coordinates) >= 3 else None
            if stamp is None or not 0 <= stamp <= 4_102_444_800_000 or depth is None:
                continue
            events.append((stamp, magnitude, depth, _text(p.get("place"), 90) or "Unknown location"))
        events.sort(key=lambda event: event[0], reverse=True)
        lines = []
        for stamp, magnitude, depth, place in events[:MAXIMUM_EARTHQUAKES]:
            clock = datetime.fromtimestamp(stamp / 1000, timezone.utc).strftime("%m-%d %H:%M UTC")
            mag = f"{magnitude:.1f}" if magnitude is not None else "N/A"
            lines.append(f"M {mag} - {place}\n{clock}\nDepth: {depth:.1f} km")
        body = "\n\n".join(lines) if lines else "No recent earthquakes reported."
        return _encoded("FIBTOOLS1\n" + body + "\n\nSource: USGS")
    if kind != DICTIONARY or not isinstance(data, list) or not data:
        raise ValueError("Invalid dictionary response")
    entry = data[0]
    if not isinstance(entry, dict) or not isinstance(entry.get("meanings"), list):
        raise ValueError("Invalid dictionary entry")
    word = _text(entry.get("word"), 48)
    lines = [word]
    for meaning in entry["meanings"]:
        if not isinstance(meaning, dict) or not isinstance(meaning.get("definitions"), list):
            continue
        for definition in meaning["definitions"]:
            if not isinstance(definition, dict):
                continue
            text = _text(definition.get("definition"), 200)
            if not text:
                continue
            section = (_text(meaning.get("partOfSpeech"), 24) or "Meaning") + ": " + text
            example = _text(definition.get("example"), 80)
            if example:
                section += "\nExample: " + example
            lines.append(section)
            break
        if len(lines) >= 4:
            break
    if not word or len(lines) == 1:
        raise ValueError("Dictionary has no definitions")
    license_info = entry.get("license")
    license_name = _text(license_info.get("name"), 40) if isinstance(license_info, dict) else ""
    license_url = _text(license_info.get("url"), 90) if isinstance(license_info, dict) else ""
    # Preserve attribution and adaptation notice on the device itself.
    sources = entry.get("sourceUrls")
    attribution = _text(sources[0], 120) if isinstance(sources, list) and sources else "Wiktionary via Free Dictionary API"
    body = "\n\n".join(lines) + "\n\nSource: " + attribution + "\n" + license_name + "\n" + license_url + "\nShortened ASCII text"
    return _encoded("FIBTOOLS1\n" + body)
