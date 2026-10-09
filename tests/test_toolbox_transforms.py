from __future__ import annotations
import json
import copy
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from host.fibp_host import toolbox
from host.fibp_host.network import perform_request
from host.fibp_host.transforms import transform_for
from scripts.fibp_codec import RequestStart

FIXTURES = Path(__file__).parent / "fixtures" / "toolbox"


class ToolboxTests(unittest.TestCase):
    def fixture(self, name):
        return (FIXTURES / (name + ".json")).read_bytes()

    def test_earthquakes_are_latest_first_and_ascii(self):
        result = toolbox.extract(toolbox.EARTHQUAKES, self.fixture("earthquakes"))
        self.assertTrue(result.startswith(b"FIBTOOLS1\nM 4.1 - Offshore"))
        self.assertIn(b"M 2.5 - Near Mugla", result)
        self.assertIn(b"Depth: 12.7 km", result)
        self.assertNotIn(b"quarry", result)
        self.assertTrue(result.isascii())
        self.assertLessEqual(len(result), 1400)

    def test_empty_earthquakes_are_explicit(self):
        self.assertIn(b"No recent earthquakes", toolbox.extract(toolbox.EARTHQUAKES, b'{"features":[]}'))

    def test_ten_latest_earthquakes_fit_existing_usb_preview(self):
        original = json.loads(self.fixture("earthquakes"))["features"][0]
        features = []
        for number in range(1, 13):
            event = copy.deepcopy(original)
            event["properties"].update(type="earthquake", time=number * 1000, mag=4.1,
                                       place=f"Event {number} " + "x" * 120)
            event["geometry"]["coordinates"][2] = 12.7
            features.append(event)
        result = toolbox.extract(toolbox.EARTHQUAKES, json.dumps({"features": features}).encode())
        self.assertEqual(result.count(b"\nM "), 10)
        self.assertTrue(result.startswith(b"FIBTOOLS1\nM 4.1 - Event 12 "))
        self.assertIn(b" - Event 3 ", result)
        self.assertNotIn(b" - Event 2 ", result)
        self.assertLessEqual(len(result), 1400)
        self.assertTrue(result.isascii())

    def test_dictionary_preserves_definitions_examples_and_attribution(self):
        result = toolbox.extract(toolbox.DICTIONARY, self.fixture("dictionary"))
        self.assertIn(b"Example: Hello, Mugla!", result)
        self.assertIn(b"someone's arrival", result)
        self.assertIn(b"CC BY-SA 3.0", result)
        self.assertIn(b"https://en.wiktionary.org/wiki/hello", result)
        self.assertTrue(result.isascii())

    def test_currency_uses_precise_bounded_wire_fields(self):
        self.assertEqual(toolbox.extract(toolbox.CURRENCY, self.fixture("currency")),
                         b"FIBRATE1\nUSD\tTRY\t49.145000000000\t2026-10-02\n")

    def test_malformed_truncated_and_oversized_sources_are_rejected(self):
        for kind, name in [(toolbox.EARTHQUAKES, "earthquakes"), (toolbox.DICTIONARY, "dictionary"), (toolbox.CURRENCY, "currency")]:
            with self.assertRaises(ValueError): toolbox.extract(kind, self.fixture(name)[:20])
            with self.assertRaises(ValueError): toolbox.extract(kind, b" " * 65537)
        for rate in (None, True, -1, 0, float("nan"), 1e10):
            data = json.loads(self.fixture("currency")); data["rate"] = rate
            with self.assertRaises(ValueError): toolbox.extract(toolbox.CURRENCY, json.dumps(data).encode())
        data = json.loads(self.fixture("currency")); data["date"] = "2026-02-31"
        with self.assertRaises(ValueError): toolbox.extract(toolbox.CURRENCY, json.dumps(data).encode())
        data["date"] = "2026-10-02"; data["rate"] = 10 ** 1000
        with self.assertRaises(ValueError): toolbox.extract(toolbox.CURRENCY, json.dumps(data).encode())
        data = json.loads(self.fixture("earthquakes"))
        data["features"] *= 5
        for feature in data["features"]:
            feature["properties"]["mag"] = 1e308
            feature["geometry"]["coordinates"][2] = 1e308
        with self.assertRaises(ValueError): toolbox.extract(toolbox.EARTHQUAKES, json.dumps(data).encode())

    def test_remote_control_codes_removed_and_outputs_bounded(self):
        data = json.loads(self.fixture("dictionary"))
        data[0]["word"] = "hello\x1b#\t\n"
        data[0]["meanings"] *= 3
        data[0]["meanings"][0]["definitions"][0]["definition"] = "x" * 4000
        result = toolbox.extract(toolbox.DICTIONARY, json.dumps(data).encode())
        self.assertNotIn(b"\x1b", result)
        self.assertNotIn(b"\t", result)
        self.assertLessEqual(len(result), 1400)

    def test_only_exact_https_endpoints_and_get_transformed(self):
        self.assertEqual(transform_for(RequestStart(1, 1000, "https://api.frankfurter.dev/v2/rate/USD/TRY")), toolbox.CURRENCY)
        self.assertEqual(transform_for(RequestStart(1, 1000, "https://api.dictionaryapi.dev/api/v2/entries/en/hello")), toolbox.DICTIONARY)
        for url in ("http://api.dictionaryapi.dev/api/v2/entries/en/hello", "https://fake.example/api/v2/entries/en/hello", "https://api.frankfurter.dev/v2/currencies", "https://api.dictionaryapi.dev/other"):
            self.assertIsNone(transform_for(RequestStart(1, 1000, url)))
        self.assertIsNone(transform_for(RequestStart(2, 1000, "https://api.frankfurter.dev/v2/rate/USD/TRY")))

    def test_unknown_word_http_404_is_preserved_without_parsing(self):
        response = Mock(status=404)
        response.getheader.return_value = None
        connection = Mock()
        starts, chunks = [], []
        with patch("host.fibp_host.network._request_once", return_value=(response, connection)):
            result = perform_request(RequestStart(1, 1000, "https://api.dictionaryapi.dev/api/v2/entries/en/unknown"), [], b"", 1536, threading.Event(),
                                     lambda *args: starts.append(args), chunks.append)
        self.assertEqual(starts, [(404, [], 0)])
        self.assertEqual(chunks, [])
        self.assertEqual(result.bytes_sent, 0)
        response.read.assert_not_called()
        connection.close.assert_called_once()


if __name__ == "__main__": unittest.main()
