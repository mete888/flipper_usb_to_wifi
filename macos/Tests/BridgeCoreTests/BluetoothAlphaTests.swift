import Foundation
import XCTest
@testable import BridgeCore

final class BluetoothAlphaTests: XCTestCase {
    func testAdvertisingBudgetAndBridgeOnlyDiscovery() {
        XCTAssertEqual(BluetoothAdvertisementPolicy.maximumAdvertisingBytes, 28)
        XCTAssertLessThanOrEqual(BluetoothAdvertisementPolicy.maximumAdvertisingBytes, 31)
        XCTAssertTrue(BluetoothAdvertisementPolicy.matches(services: ["3080"], localName: "FZ Bridge Mico"))
        XCTAssertTrue(BluetoothAdvertisementPolicy.matches(services: ["3080"], localName: "FZ Bridge ABCDEF"))
        for name in ["Flipper Mico", "Bluetooth Device", "FZ Bridge ", "FZ Bridge TOOLONG", "FZ Bridge Mi\n"] {
            XCTAssertFalse(BluetoothAdvertisementPolicy.matches(services: ["3080"], localName: name))
        }
        XCTAssertFalse(BluetoothAdvertisementPolicy.matches(services: ["180F"], localName: "FZ Bridge Mico"))
        XCTAssertFalse(BluetoothAdvertisementPolicy.matches(services: ["3080"], localName: nil))
    }
    func testBLEFragmentsAreStillOneCRCCheckedFIBPByteStream() throws {
        let frames = [
            FIBPFrame(messageType: .ping, requestID: 0, sequence: 1, payload: Data(repeating: 7, count: 8)),
            FIBPFrame(messageType: .responseBodyChunk, requestID: 5, sequence: 2, payload: Data(repeating: 65, count: 192)),
            FIBPFrame(messageType: .responseEnd, flags: [.final], requestID: 5, sequence: 3, payload: Data([0, 192, 0, 0, 0]))
        ]
        let wire = try frames.reduce(into: Data()) { $0.append(try FIBPCodec.encode($1)) }
        for fragmentSize in [1, 20, 128] {
            let parser = FIBPStreamParser()
            var actual = [FIBPFrame]()
            for offset in stride(from: 0, to: wire.count, by: fragmentSize) {
                for event in parser.feed(wire.subdata(in: offset..<min(wire.count, offset + fragmentSize))) {
                    if case let .frame(frame) = event { actual.append(frame) }
                    else { XCTFail("Unexpected parser error") }
                }
            }
            XCTAssertEqual(actual, frames)
        }
    }

    func testDisabledAlphaNeverCreatesCentralAndRejectsWrites() {
        // No start(): these tests must not trigger an OS Bluetooth prompt.
        let transport = BluetoothAlphaTransport()
        XCTAssertFalse(transport.isOpen)
        XCTAssertThrowsError(try transport.send(Data([1, 2, 3])))
        XCTAssertThrowsError(try transport.send(Data(repeating: 0, count: 545)))
        XCTAssertThrowsError(try transport.open(device: SerialDevice(registryID: 1,
            calloutPath: "ble-alpha:test", linkKind: .bluetoothAlpha)))
    }

    func testUSBRemainsDefaultAndAlphaDoesNotFabricateUSBIdentity() {
        XCTAssertEqual(SerialDevice(registryID: 1, calloutPath: "/dev/cu.test").linkKind, .usb)
        let alpha = SerialDevice(registryID: 1, calloutPath: "ble-alpha:test", linkKind: .bluetoothAlpha)
        XCTAssertNil(alpha.vendorID)
        XCTAssertNil(alpha.productID)
        XCTAssertNil(alpha.usbSerialNumber)
        XCTAssertNotEqual(alpha.linkKind, .usb)
    }
}
