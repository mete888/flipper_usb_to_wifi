import Foundation
import XCTest
@testable import BridgeCore

/// No unit test writes to the user's actual Keychain or Bluetooth bonds.
final class MemoryBluetoothVault: BluetoothCredentialStorage {
    private let lock = NSLock()
    private var items = [UUID: Data]()
    var refuseWrites = false, refuseDeletes = false
    func read(_ id: UUID) throws -> Data? {
        lock.lock(); defer { lock.unlock() }; return items[id]
    }
    func write(_ data: Data, for id: UUID) throws {
        lock.lock(); defer { lock.unlock() }
        if refuseWrites { throw BluetoothPairingError.keychain(-1) }
        items[id] = data
    }
    func remove(_ id: UUID) throws {
        lock.lock(); defer { lock.unlock() }
        if refuseDeletes { throw BluetoothPairingError.keychain(-1) }
        items.removeValue(forKey: id)
    }
}

final class BluetoothPairingStoreTests: XCTestCase {
    let uid = Data([1, 2, 3]), token = Data(repeating: 7, count: 32)
    func testPersistenceAndDeviceIdentityChange() throws {
        let suite = "fib-pairing-test-\(UUID())", vault = MemoryBluetoothVault(), id = UUID()
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = BluetoothPairingStore(defaults: defaults, secrets: vault)
        try store.establish(id: id, name: "Mico", deviceID: uid, token: token)
        let restarted = BluetoothPairingStore(defaults: defaults, secrets: vault)
        XCTAssertEqual(store.hostID, restarted.hostID)
        XCTAssertEqual(restarted.snapshot().map(\.name), ["Mico"])
        XCTAssertEqual(try restarted.token(for: id, deviceID: uid), token)
        XCTAssertNil(try restarted.token(for: id, deviceID: Data([9])))
    }
    func testRevokeDeletesAndLateHelloCannotReinsert() throws {
        let vault = MemoryBluetoothVault(), first = UUID(), second = UUID()
        let store = BluetoothPairingStore(defaults: nil, secrets: vault)
        try store.establish(id: first, name: "Mico", deviceID: uid, token: token)
        try store.establish(id: second, name: "Second", deviceID: uid, token: token)
        try store.revoke(first)
        store.renameRecognizedPeer(id: first, name: "Late HELLO")
        XCTAssertEqual(store.snapshot().map(\.id), [second])
        XCTAssertNil(try vault.read(first))
        XCTAssertNil(try store.token(for: first, deviceID: uid))
        XCTAssertEqual(try store.token(for: second, deviceID: uid), token)
    }
    func testFailedDeletionDoesNotPretendToRevoke() throws {
        let vault = MemoryBluetoothVault(), id = UUID()
        let store = BluetoothPairingStore(defaults: nil, secrets: vault)
        try store.establish(id: id, name: "Mico", deviceID: uid, token: token)
        vault.refuseDeletes = true
        XCTAssertThrowsError(try store.revoke(id))
        XCTAssertEqual(store.snapshot().count, 1)
        XCTAssertEqual(try store.token(for: id, deviceID: uid), token)
    }
    func testInternetAndUSBGrantsStayIndependent() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault()), id = UUID()
        let permissions = InMemoryPermissionStore()
        let identity = FlipperIdentity(model: "Flipper Zero", name: "Mico", idType: 1,
            deviceID: uid, appVersion: "0.5.0", protocolMajor: 1, protocolMinor: 0)
        try store.establish(id: id, name: "Mico", deviceID: uid, token: token)
        permissions.grant(identity); permissions.revoke(identity)
        XCTAssertEqual(try store.token(for: id, deviceID: uid), token)
        permissions.grant(identity); try store.revoke(id)
        XCTAssertTrue(permissions.contains(identity))
    }
    func testBoundAndSanitizedNames() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault())
        try store.establish(id: UUID(), name: "Mi\u{202E}co\u{200B}", deviceID: uid, token: token)
        for _ in 0..<63 {
            try store.establish(id: UUID(), name: String(repeating: "A", count: 500), deviceID: uid, token: token)
        }
        XCTAssertTrue(store.snapshot().contains { $0.name == "Mico" })
        XCTAssertTrue(store.snapshot().allSatisfy { $0.name.count <= 32 })
        XCTAssertThrowsError(try store.establish(id: UUID(), name: "Overflow", deviceID: uid, token: token))
        try store.revoke(try XCTUnwrap(store.snapshot().first).id)
        try store.establish(id: UUID(), name: "Replacement", deviceID: uid, token: token)
        XCTAssertEqual(store.snapshot().count, 64)
    }
    func testConcurrentRevokeAndRenameNeverReinserts() throws {
        let store = BluetoothPairingStore(defaults: nil, secrets: MemoryBluetoothVault()), id = UUID()
        try store.establish(id: id, name: "Mico", deviceID: uid, token: token)
        DispatchQueue.concurrentPerform(iterations: 100) { i in
            if i.isMultiple(of: 2) { try? store.revoke(id) }
            else { store.renameRecognizedPeer(id: id, name: "Mico") }
            _ = store.snapshot()
        }
        XCTAssertTrue(store.snapshot().isEmpty)
    }
    func testLegacyObservedDevicesAreNotTrustedAndZeroTokenIsRejected() throws {
        let suite = "fib-pairing-test-\(UUID())"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        defaults.set(Data("[{\"id\":\"\(UUID())\",\"name\":\"Mico\",\"revoked\":false}]".utf8),
                     forKey: "fib.bluetooth-alpha.peers.v1")
        let store = BluetoothPairingStore(defaults: defaults, secrets: MemoryBluetoothVault())
        XCTAssertTrue(store.snapshot().isEmpty)
        XCTAssertThrowsError(try store.establish(id: UUID(), name: "Mico", deviceID: uid, token: Data(repeating: 0, count: 32)))
    }
}
