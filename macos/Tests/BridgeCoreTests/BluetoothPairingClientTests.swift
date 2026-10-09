import Foundation
import XCTest
@testable import BridgeCore

final class BluetoothPairingClientTests: XCTestCase {
    let now = Date(timeIntervalSince1970: 1234)
    let uid = Data([1, 2, 3])
    func hello(capability: UInt32 = BluetoothPairingClient.capability | BluetoothPairingClient.selectionCapability) throws -> FIBPFrame {
        let value = FIBPHello(capabilities: FIBPCapabilities(rawValue: capability),
            maximumReceivePayload: 512, maximumResponseBytes: 8192, clientNonce: 0x0807060504030201,
            model: "Flipper Zero", name: "Mico", idType: 1, deviceID: uid, appVersion: "0.5.0")
        return FIBPFrame(messageType: .hello, requestID: 0, sequence: 0, payload: try value.encoded())
    }
    func reply(_ type: UInt8, _ status: UInt8) -> FIBPFrame {
        FIBPFrame(major: 1, minor: 0, rawMessageType: type, flags: [], requestID: 0,
                  sequence: 0, payload: Data([1, 2, 3, 4, 5, 6, 7, 8, status]))
    }
    func establish(_ client: BluetoothPairingClient) throws -> FIBPFrame {
        _ = try client.begin(hello(), now: now)
        XCTAssertFalse(try client.receive(reply(0x45, 3), now: now))
        XCTAssertTrue(try client.receive(reply(0x42, 2), now: now))
        let code = try client.submit("123456", now: now)
        XCTAssertFalse(try client.receive(reply(0x43, 1), now: now))
        return code
    }
    func testNewDeviceWireLayoutAndRecognition() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault()), id = UUID()
        let client = BluetoothPairingClient(store: store, peer: id)
        let open = try client.begin(hello(), now: now)
        XCTAssertEqual(open.rawMessageType, 0x40)
        XCTAssertEqual(open.payload.count, 56)
        XCTAssertEqual(open.payload.prefix(8), Data([1, 2, 3, 4, 5, 6, 7, 8]))
        XCTAssertEqual(open.payload.subdata(in: 8..<24), store.hostID)
        XCTAssertEqual(open.payload.suffix(32), Data(repeating: 0, count: 32))
        XCTAssertNil(client.revokePacket())
        XCTAssertFalse(try client.receive(reply(0x45, 3), now: now))
        XCTAssertTrue(client.awaitingFlipperSelection)
        XCTAssertFalse(client.accepted)
        XCTAssertThrowsError(try client.submit("123456", now: now))
        XCTAssertTrue(try client.receive(reply(0x42, 2), now: now))
        XCTAssertTrue(store.snapshot().isEmpty)
        let code = try client.submit("123456", now: now)
        XCTAssertEqual(code.rawMessageType, 0x41)
        XCTAssertEqual(code.payload.count, 62)
        XCTAssertEqual(code.payload.subdata(in: 24..<30), Data("123456".utf8))
        XCTAssertFalse(try client.receive(reply(0x43, 1), now: now))
        XCTAssertTrue(client.accepted)
        XCTAssertEqual(try store.token(for: id, deviceID: uid), Data(code.payload.suffix(32)))
        XCTAssertEqual(client.revokePacket()?.payload.count, 24)
    }
    func testKnownPeerSkipsCodeRevokedPeerRequiresCode() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault()), id = UUID()
        let code = try establish(BluetoothPairingClient(store: store, peer: id))
        let known = BluetoothPairingClient(store: store, peer: id)
        XCTAssertEqual(try known.begin(hello(), now: now).payload.suffix(32), code.payload.suffix(32))
        XCTAssertFalse(try known.receive(reply(0x45, 3), now: now))
        XCTAssertFalse(known.accepted)
        XCTAssertFalse(try known.receive(reply(0x43, 1), now: now))
        try store.revoke(id)
        let removed = BluetoothPairingClient(store: store, peer: id)
        XCTAssertEqual(try removed.begin(hello(), now: now).payload.suffix(32), Data(repeating: 0, count: 32))
        XCTAssertFalse(try removed.receive(reply(0x45, 3), now: now))
        XCTAssertTrue(try removed.receive(reply(0x42, 2), now: now))
        XCTAssertFalse(removed.accepted)
        XCTAssertTrue(store.snapshot().isEmpty)
    }
    func testFlipperCredentialLossForcesNewCode() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault()), id = UUID()
        _ = try establish(BluetoothPairingClient(store: store, peer: id))
        let reconnect = BluetoothPairingClient(store: store, peer: id)
        _ = try reconnect.begin(hello(), now: now)
        XCTAssertFalse(try reconnect.receive(reply(0x45, 3), now: now))
        XCTAssertTrue(try reconnect.receive(reply(0x42, 2), now: now))
        XCTAssertTrue(store.snapshot().isEmpty)
        let changed = try reconnect.submit("654321", now: now)
        _ = try reconnect.receive(reply(0x43, 1), now: now)
        XCTAssertEqual(try store.token(for: id, deviceID: uid), Data(changed.payload.suffix(32)))
    }
    func testRejectLegacyStaleNonceWrongEnvelopeAndOrder() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault())
        XCTAssertThrowsError(try BluetoothPairingClient(store: store, peer: UUID()).begin(hello(capability: 0), now: now))
        XCTAssertThrowsError(try BluetoothPairingClient(store: store, peer: UUID()).begin(hello(capability: BluetoothPairingClient.capability), now: now))
        let client = BluetoothPairingClient(store: store, peer: UUID())
        _ = try client.begin(hello(), now: now)
        var bad = reply(0x42, 2); bad.payload[0] = 9
        XCTAssertThrowsError(try client.receive(bad, now: now))
        bad = reply(0x42, 2); bad.flags = [.final]
        XCTAssertThrowsError(try client.receive(bad, now: now))
        bad = reply(0x42, 2); bad.major = 2
        XCTAssertThrowsError(try client.receive(bad, now: now))
        bad = reply(0x42, 2); bad.payload.removeLast()
        XCTAssertThrowsError(try client.receive(bad, now: now))
        XCTAssertThrowsError(try client.submit("123456", now: now))
        XCTAssertThrowsError(try client.receive(reply(0x43, 1), now: now))
    }
    func testTimeoutWrongCodesAndFailedStorageCannotTrust() throws {
        let vault = MemoryBluetoothVault()
        let store = BluetoothPairingStore(defaults: nil, secrets: vault)
        let client = BluetoothPairingClient(store: store, peer: UUID())
        _ = try client.begin(hello(), now: now)
        XCTAssertFalse(try client.receive(reply(0x45, 3), now: now))
        XCTAssertTrue(try client.receive(reply(0x42, 2), now: now))
        for bad in ["12345", "abcdef", "1234567", "１２３４５６"] {
            XCTAssertThrowsError(try client.submit(bad, now: now))
        }
        _ = try client.submit("111111", now: now)
        XCTAssertTrue(try client.receive(reply(0x42, 2), now: now))
        _ = try client.submit("123456", now: now)
        vault.refuseWrites = true
        XCTAssertThrowsError(try client.receive(reply(0x43, 1), now: now))
        XCTAssertFalse(client.accepted)
        XCTAssertTrue(store.snapshot().isEmpty)
        XCTAssertThrowsError(try client.receive(reply(0x43, 1), now: now.addingTimeInterval(120)))
    }
    func testFragmentedPairingMessagesAreCRCChecked() throws {
        let client = BluetoothPairingClient(store: BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault()), peer: UUID())
        let frame = try client.begin(hello(), now: now)
        let wire = try FIBPCodec.encode(frame), parser = FIBPStreamParser()
        var decoded = [FIBPFrame]()
        for i in stride(from: 0, to: wire.count, by: 20) {
            for event in parser.feed(wire.subdata(in: i..<min(i + 20, wire.count))) {
                if case .frame(let f) = event { decoded.append(f) } else { XCTFail() }
            }
        }
        XCTAssertEqual(decoded, [frame])
        var broken = wire; broken[broken.count - 1] ^= 1
        XCTAssertTrue(parser.feed(broken).contains { if case .error = $0 { return true }; return false })
    }
    func testFlipperRevocationNoticeIsBoundToNonceAndHost() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault())
        let client = BluetoothPairingClient(store: store, peer: UUID())
        let open = try client.begin(hello(), now: now)
        let notice = FIBPFrame(major: 1, minor: 0, rawMessageType: 0x46, flags: [],
            requestID: 0, sequence: 0, payload: Data(open.payload.prefix(24)))
        XCTAssertTrue(client.isRevocationNotice(notice))
        var bad = notice; bad.payload[8] ^= 1
        XCTAssertFalse(client.isRevocationNotice(bad))
        bad = notice; bad.payload[0] ^= 1
        XCTAssertFalse(client.isRevocationNotice(bad))
        bad = notice; bad.sequence = 1
        XCTAssertFalse(client.isRevocationNotice(bad))
        bad = notice; bad.flags = [.final]
        XCTAssertFalse(client.isRevocationNotice(bad))
        bad = notice; bad.payload.removeLast()
        XCTAssertFalse(client.isRevocationNotice(bad))
    }
    func testSelectionExpiryRejectionAndSeparateCodeDeadline() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault())
        let rejected = BluetoothPairingClient(store: store, peer: UUID())
        _ = try rejected.begin(hello(), now: now)
        _ = try rejected.receive(reply(0x45, 3), now: now)
        XCTAssertThrowsError(try rejected.receive(reply(0x45, 3), now: now)) // replay
        XCTAssertThrowsError(try rejected.receive(reply(0x43, 0), now: now))
        XCTAssertFalse(rejected.accepted)
        XCTAssertTrue(store.snapshot().isEmpty)
        let expired = BluetoothPairingClient(store: store, peer: UUID())
        _ = try expired.begin(hello(), now: now)
        XCTAssertThrowsError(try expired.receive(reply(0x45, 3), now: now.addingTimeInterval(120)))
        let late = BluetoothPairingClient(store: store, peer: UUID())
        _ = try late.begin(hello(), now: now)
        XCTAssertThrowsError(try late.receive(reply(0x42, 2), now: now)) // no selection-wait packet
        _ = try late.receive(reply(0x45, 3), now: now)
        XCTAssertTrue(try late.receive(reply(0x42, 2), now: now.addingTimeInterval(119)))
        XCTAssertEqual(late.expiresAt, now.addingTimeInterval(239))
        _ = try late.submit("111111", now: now.addingTimeInterval(150))
        _ = try late.receive(reply(0x42, 2), now: now.addingTimeInterval(151))
        XCTAssertEqual(late.expiresAt, now.addingTimeInterval(239)) // wrong code doesn't renew
        XCTAssertThrowsError(try late.submit("123456", now: now.addingTimeInterval(239)))
    }
}
