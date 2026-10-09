import Foundation
import XCTest
@testable import BridgeCore

final class RadioPCMDecoderTests: XCTestCase {
    func testInvalidInputAndSeparateDecoderLifetimes() throws {
        for _ in 0..<30 {
            let decoder = try XCTUnwrap(RadioPCMDecoder())
            XCTAssertEqual(decoder.feed(Data(repeating: 0x11, count: 16384)), Data())
            XCTAssertEqual(decoder.feed(Data()), Data())
        }
        XCTAssertTrue(FIBPCapabilities.helperSupported.contains(.usbRadioPCM))
        XCTAssertEqual(FIBPCapabilities.usbRadioPCM.rawValue & 0x380, 0)
    }

    func testOptInFixtureDecodesIdenticallyAcrossFragments() throws {
        guard let path = ProcessInfo.processInfo.environment["FIB_RADIO_FIXTURE"] else {
            throw XCTSkip("Set FIB_RADIO_FIXTURE to an MP3 fixture; no tone is played on Flipper.")
        }
        let mp3 = try Data(contentsOf: URL(fileURLWithPath: path))
        let first = try XCTUnwrap(RadioPCMDecoder())
        let second = try XCTUnwrap(RadioPCMDecoder())
        let reference = first.feed(mp3)
        var fragmented = Data()
        for offset in stride(from: 0, to: mp3.count, by: 97) {
            fragmented.append(second.feed(mp3.subdata(in: offset..<min(offset + 97, mp3.count))))
        }
        XCTAssertEqual(reference, fragmented)
        XCTAssertGreaterThan(reference.count, 20000)
        XCTAssertLessThan(reference.count, 32000)
        XCTAssertEqual(reference.count % 2, 0)
        XCTAssertTrue(reference.contains(where: { $0 != 0 }))
    }
}
