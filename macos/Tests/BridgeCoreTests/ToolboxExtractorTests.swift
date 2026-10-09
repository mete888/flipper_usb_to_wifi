import Foundation
import XCTest
@testable import BridgeCore

final class ToolboxExtractorTests: XCTestCase {
    private func fixture(_ name: String) throws -> Data {
        var root = URL(fileURLWithPath: #filePath)
        for _ in 0..<4 { root.deleteLastPathComponent() }
        return try Data(contentsOf: root.appendingPathComponent("tests/fixtures/toolbox/\(name).json"))
    }
    private func display(_ kind: ToolboxExtractor.Kind, _ name: String) throws -> String {
        let data = try XCTUnwrap(ToolboxExtractor.extract(kind, from: fixture(name)))
        XCTAssertLessThanOrEqual(data.count, 1400)
        XCTAssertTrue(data.allSatisfy { $0 < 128 })
        return String(decoding: data, as: UTF8.self)
    }
    func testLatestEarthquakesSortAndTransliteration() throws {
        let text = try display(.earthquakes, "earthquakes")
        XCTAssertTrue(text.hasPrefix("FIBTOOLS1\nM 4.1 - Offshore"))
        XCTAssertTrue(text.contains("M 2.5 - Near Mugla"))
        XCTAssertTrue(text.contains("Depth: 12.7 km"))
        XCTAssertFalse(text.contains("quarry"))
    }
    func testDictionaryDefinitionsAndAttribution() throws {
        let text = try display(.dictionary, "dictionary")
        XCTAssertTrue(text.contains("Example: Hello, Mugla!"))
        XCTAssertTrue(text.contains("someone's arrival"))
        XCTAssertTrue(text.contains("CC BY-SA 3.0"))
        XCTAssertTrue(text.contains("https://en.wiktionary.org/wiki/hello"))
    }
    func testTenLatestEarthquakesFitExistingUSBPreview() throws {
        var features = [[String: Any]]()
        for number in 1...12 {
            features.append([
                "properties": ["type": "earthquake", "time": number * 1000,
                               "mag": 4.1, "place": "Event \(number) " + String(repeating: "x", count: 120)],
                "geometry": ["coordinates": [0.0, 0.0, 12.7]],
            ])
        }
        let json = try JSONSerialization.data(withJSONObject: ["features": features])
        let data = try XCTUnwrap(ToolboxExtractor.extract(.earthquakes, from: json))
        let text = String(decoding: data, as: UTF8.self)
        XCTAssertEqual(text.components(separatedBy: "\nM ").count - 1, 10)
        XCTAssertTrue(text.hasPrefix("FIBTOOLS1\nM 4.1 - Event 12 "))
        XCTAssertTrue(text.contains(" - Event 3 "))
        XCTAssertFalse(text.contains(" - Event 2 "))
        XCTAssertLessThanOrEqual(data.count, 1400)
        XCTAssertTrue(data.allSatisfy { $0 < 128 })
    }
    func testCurrencyMatchesPortableWireFormat() throws {
        XCTAssertEqual(try display(.currency, "currency"), "FIBRATE1\nUSD\tTRY\t49.145000000000\t2026-10-02\n")
    }
    func testMalformedAndOversizedDataRejected() throws {
        for (kind, file) in [(ToolboxExtractor.Kind.earthquakes, "earthquakes"), (.dictionary, "dictionary"), (.currency, "currency")] {
            XCTAssertNil(ToolboxExtractor.extract(kind, from: try fixture(file).prefix(20)))
            XCTAssertNil(ToolboxExtractor.extract(kind, from: Data(repeating: 32, count: 65537)))
        }
        for rate in ["null", "true", "-1", "0", "10000000000"] {
            let json = "{\"date\":\"2026-10-02\",\"base\":\"USD\",\"quote\":\"TRY\",\"rate\":\(rate)}"
            XCTAssertNil(ToolboxExtractor.extract(.currency, from: Data(json.utf8)))
        }
        XCTAssertNil(ToolboxExtractor.extract(.currency, from: Data("{\"date\":\"2026-02-31\",\"base\":\"USD\",\"quote\":\"TRY\",\"rate\":1}".utf8)))
    }
    func testEmptyFeedAndUnknownWordResponse() {
        let text = ToolboxExtractor.extract(.earthquakes, from: Data("{\"features\":[]}".utf8))
        XCTAssertTrue(String(decoding: text ?? Data(), as: UTF8.self).contains("No recent earthquakes"))
        XCTAssertNil(ToolboxExtractor.extract(.dictionary, from: Data("{\"title\":\"No Definitions Found\"}".utf8)))
    }
    func testOnlyExactHTTPSGETEndpointsHandled() {
        func request(_ url: String, method: FIBPHTTPMethod = .get) -> BridgeHTTPRequest {
            BridgeHTTPRequest(requestID: 1, method: method, urlString: url, headers: [], body: Data(), timeout: 5)
        }
        XCTAssertNotNil(ToolboxExtractor.kind(for: request("https://api.dictionaryapi.dev/api/v2/entries/en/hello")))
        XCTAssertNotNil(ToolboxExtractor.kind(for: request("https://api.frankfurter.dev/v2/rate/USD/TRY")))
        XCTAssertNil(ToolboxExtractor.kind(for: request("http://api.dictionaryapi.dev/api/v2/entries/en/hello")))
        XCTAssertNil(ToolboxExtractor.kind(for: request("https://fake.example/v2/rate/USD/TRY")))
        XCTAssertNil(ToolboxExtractor.kind(for: request("https://api.frankfurter.dev/v2/rate/USD/TRY", method: .post)))
    }

    // External services are opt-in: ordinary unit tests and CI stay offline.
    private func liveFetch(_ url: String) throws -> (Int, Data) {
        guard ProcessInfo.processInfo.environment["FIB_TOOLBOX_LIVE"] == "1" else {
            throw XCTSkip("Set FIB_TOOLBOX_LIVE=1 to test public HTTPS providers.")
        }
        let client = HTTPSNetworkClient(policy: NetworkPolicy())
        defer { client.invalidate() }
        let done = expectation(description: "Toolbox HTTPS response")
        let lock = NSLock()
        var status = 0
        var body = Data()
        var outcome: Result<Void, BridgeNetworkError>?
        client.execute(
            BridgeHTTPRequest(requestID: 1, method: .get, urlString: url,
                              headers: [], body: Data(), timeout: 15),
            onResponse: { metadata in
                lock.lock(); defer { lock.unlock() }
                status = metadata.statusCode
            },
            onData: { data in
                lock.lock(); defer { lock.unlock() }
                body.append(data)
            },
            completion: { result in
                lock.lock(); outcome = result; lock.unlock()
                done.fulfill()
            }
        )
        wait(for: [done], timeout: 20)
        lock.lock(); defer { lock.unlock() }
        let result = try XCTUnwrap(outcome)
        try result.get()
        return (status, body)
    }

    func testLiveProvidersThroughActualNetworkClient() throws {
        let cases = [
            ("https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&orderby=time&limit=10&eventtype=earthquake", "FIBTOOLS1\n"),
            ("https://api.frankfurter.dev/v2/rate/USD/TRY", "FIBRATE1\nUSD\tTRY\t"),
            ("https://api.dictionaryapi.dev/api/v2/entries/en/hello", "FIBTOOLS1\nhello\n"),
        ]
        for (url, prefix) in cases {
            let (status, body) = try liveFetch(url)
            XCTAssertEqual(status, 200, url)
            XCTAssertTrue(String(decoding: body, as: UTF8.self).hasPrefix(prefix), url)
            XCTAssertTrue(body.allSatisfy { $0 < 128 }, url)
            XCTAssertLessThanOrEqual(body.count, 1400, url)
            if url.contains("earthquake.usgs.gov") {
                XCTAssertEqual(String(decoding: body, as: UTF8.self).components(separatedBy: "\nM ").count - 1, 10)
            }
        }
    }

    func testLiveUnknownDictionaryWordPreserves404() throws {
        let (status, body) = try liveFetch("https://api.dictionaryapi.dev/api/v2/entries/en/zzzznotawordxxxx")
        XCTAssertEqual(status, 404)
        XCTAssertTrue(body.isEmpty)
    }
}
