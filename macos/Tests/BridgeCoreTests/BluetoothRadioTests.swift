import Foundation
import XCTest
@testable import BridgeCore

final class BluetoothRadioTests: XCTestCase {
    func testRadioCapabilitiesAreNotAdvertised() {
        XCTAssertEqual(FIBPCapabilities.helperSupported.rawValue & 0x380, 0)
    }

    func testNetworkClientRefusesBluetoothRadioBeforeDNS() {
        let client = HTTPSNetworkClient(policy: NetworkPolicy())
        defer { client.invalidate() }
        let done = expectation(description: "Bluetooth radio rejected")
        client.execute(.init(requestID: 1, method: .get, urlString: "https://localhost/",
            headers: [.init(name: "accept", value: "audio/mpeg; fib-adpcm=4000")],
            body: Data(), timeout: 1, bluetooth: true),
            onResponse: { _ in XCTFail("Must not deliver a radio response") },
            onData: { _ in XCTFail("Must not deliver audio") },
            completion: { result in
                if case .failure(.invalidResponse) = result {} else { XCTFail("Expected radio rejection: \(result)") }
                done.fulfill()
            })
        wait(for: [done], timeout: 2)
    }

    func testBluetoothRequestAlwaysHasTextLimit() {
        let request = BridgeHTTPRequest(requestID: 1, method: .get,
            urlString: "https://example.com/", headers: [], body: Data(),
            timeout: 1, bluetooth: true)
        XCTAssertEqual(request.maximumResponseBytes, 8192)
    }

    func testOptInUSBMP3NetworkStreamAndCancel() throws {
        guard let url = ProcessInfo.processInfo.environment["FIB_USB_RADIO_URL"] else {
            throw XCTSkip("Set FIB_USB_RADIO_URL for a live USB MP3 network check (not speaker certification).")
        }
        let client = HTTPSNetworkClient(policy: NetworkPolicy())
        defer { client.invalidate() }
        let done = expectation(description: "MP3 delivered then cancelled")
        let lock = NSLock()
        var count = 0
        client.execute(.init(requestID: 1, method: .get, urlString: url,
            headers: [.init(name: "accept", value: "audio/mpeg")], body: Data(), timeout: 30),
            onResponse: { XCTAssertEqual($0.statusCode, 200) },
            onData: { data in
                lock.lock(); count += data.count; let enough = count >= 16384; lock.unlock()
                if enough { client.cancel() }
            }, completion: { result in
                lock.lock(); let received = count; lock.unlock()
                XCTAssertGreaterThanOrEqual(received, 16384)
                if case .failure(.cancelled) = result {} else { XCTFail("Expected cancellation: \(result)") }
                done.fulfill()
            })
        wait(for: [done], timeout: 55)
    }

    func testOptInUSBPCMNetworkStreamAndCancel() throws {
        guard let url = ProcessInfo.processInfo.environment["FIB_USB_RADIO_URL"] else {
            throw XCTSkip("Set FIB_USB_RADIO_URL for live USB PCM decoding and cancellation.")
        }
        let client = HTTPSNetworkClient(policy: NetworkPolicy())
        defer { client.invalidate() }
        let done = expectation(description: "PCM delivered then cancelled")
        let lock = NSLock()
        var count = 0
        client.execute(.init(requestID: 2, method: .get, urlString: url,
            headers: [.init(name: "accept", value: "audio/mpeg; fib-pcm=14493")], body: Data(), timeout: 30),
            onResponse: {
                XCTAssertEqual($0.statusCode, 200)
                XCTAssertEqual($0.headers.first?.value, "audio/x-fib-pcm;rate=14493;channels=1;format=s16le")
                XCTAssertEqual($0.expectedBodyLength, -1)
            }, onData: { data in
                XCTAssertEqual(data.count % 2, 0)
                lock.lock(); count += data.count; let enough = count >= 32768; lock.unlock()
                if enough { client.cancel() }
            }, completion: { result in
                lock.lock(); let received = count; lock.unlock()
                XCTAssertGreaterThanOrEqual(received, 32768)
                if case .failure(.cancelled) = result {} else { XCTFail("Expected cancellation: \(result)") }
                done.fulfill()
            })
        wait(for: [done], timeout: 55)
    }
}
