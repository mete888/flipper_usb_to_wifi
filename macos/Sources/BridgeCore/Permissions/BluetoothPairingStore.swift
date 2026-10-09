import CryptoKit
import Foundation
import Security

public struct BluetoothPairingRecord: Codable, Identifiable, Equatable {
    public let id: UUID
    public var name: String
}

public protocol BluetoothCredentialStorage {
    func read(_ id: UUID) throws -> Data?
    func write(_ data: Data, for id: UUID) throws
    func remove(_ id: UUID) throws
}

/// Credentials, unlike public display metadata, belong in Keychain.
public final class KeychainBluetoothCredentials: BluetoothCredentialStorage {
    public init() {}
    private func query(_ id: UUID) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: "FlipperInternetBridge.BluetoothRecognition.v2",
         kSecAttrAccount as String: id.uuidString]
    }
    public func read(_ id: UUID) throws -> Data? {
        var q = query(id)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(q as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess else { throw BluetoothPairingError.keychain(status) }
        guard let data = result as? Data else { throw BluetoothPairingError.malformed }
        return data
    }
    public func write(_ data: Data, for id: UUID) throws {
        let status = SecItemUpdate(query(id) as CFDictionary,
                                  [kSecValueData as String: data] as CFDictionary)
        if status == errSecItemNotFound {
            var q = query(id)
            q[kSecValueData as String] = data
            q[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            let added = SecItemAdd(q as CFDictionary, nil)
            guard added == errSecSuccess else { throw BluetoothPairingError.keychain(added) }
        } else if status != errSecSuccess { throw BluetoothPairingError.keychain(status) }
    }
    public func remove(_ id: UUID) throws {
        let status = SecItemDelete(query(id) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw BluetoothPairingError.keychain(status)
        }
    }
}

public enum BluetoothPairingError: LocalizedError {
    case keychain(OSStatus), malformed, incompatible, rejected, timeout, full
    public var errorDescription: String? {
        switch self {
        case .keychain: return "Bluetooth credentials could not be accessed in Keychain."
        case .malformed: return "Invalid Bluetooth pairing message or code."
        case .incompatible: return "Update both the Flipper FAP and Mac helper to use Bluetooth pairing."
        case .rejected: return "Bluetooth pairing was rejected. Reconnect Bluetooth to retry."
        case .timeout: return "Bluetooth pairing code expired. Reconnect Bluetooth to retry."
        case .full: return "Remove an old Bluetooth pairing before adding another device."
        }
    }
}

/// Bridge recognition only: neither OS bonds nor internet grants. Revocation
/// really deletes a record; delayed HELLO callbacks cannot recreate it.
public final class BluetoothPairingStore {
    private static let key = "fib.bluetooth-alpha.peers.v2"
    private static let hostKey = "fib.bluetooth-alpha.host.v2"
    private let lock = NSLock()
    private let defaults: UserDefaults?
    private let secrets: BluetoothCredentialStorage
    private var records: [BluetoothPairingRecord]
    public let hostID: Data

    public init(defaults: UserDefaults? = .standard,
                secrets: BluetoothCredentialStorage = KeychainBluetoothCredentials()) {
        self.defaults = defaults; self.secrets = secrets
        if let saved = defaults?.data(forKey: Self.hostKey), saved.count == 16 { hostID = saved }
        else {
            hostID = Self.randomBytes(16)
            defaults?.set(hostID, forKey: Self.hostKey)
        }
        // v1 records described merely observed peripherals, not authenticated
        // bridge pairings. Never migrate them into trusted credentials.
        let decoded = defaults?.data(forKey: Self.key).flatMap {
            try? JSONDecoder().decode([BluetoothPairingRecord].self, from: $0)
        } ?? []
        var ids = Set<UUID>()
        records = decoded.prefix(64).filter { ids.insert($0.id).inserted }
            .map { BluetoothPairingRecord(id: $0.id, name: Self.safeName($0.name)) }
    }
    public static func randomBytes(_ count: Int) -> Data {
        precondition((1...32).contains(count))
        return SymmetricKey(size: .bits256).withUnsafeBytes { Data($0.prefix(count)) }
    }
    public func snapshot() -> [BluetoothPairingRecord] {
        lock.lock(); defer { lock.unlock() }
        return records.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }
    public func token(for id: UUID, deviceID: Data) throws -> Data? {
        lock.lock(); defer { lock.unlock() }
        guard records.contains(where: { $0.id == id }), let data = try secrets.read(id),
              data.count == 80, data.prefix(16) == hostID,
              data.suffix(32) == Data(SHA256.hash(data: deviceID)) else { return nil }
        return data.subdata(in: 16..<48)
    }
    public func establish(id: UUID, name: String, deviceID: Data, token: Data) throws {
        guard token.count == 32, token.contains(where: { $0 != 0 }) else { throw BluetoothPairingError.malformed }
        lock.lock(); defer { lock.unlock() }
        guard records.count < 64 || records.contains(where: { $0.id == id }) else { throw BluetoothPairingError.full }
        try secrets.write(hostID + token + Data(SHA256.hash(data: deviceID)), for: id)
        if let i = records.firstIndex(where: { $0.id == id }) { records[i].name = Self.safeName(name) }
        else { records.append(BluetoothPairingRecord(id: id, name: Self.safeName(name))) }
        persist()
    }
    public func renameRecognizedPeer(id: UUID, name: String) {
        lock.lock(); defer { lock.unlock() }
        guard let i = records.firstIndex(where: { $0.id == id }) else { return }
        records[i].name = Self.safeName(name); persist()
    }
    public func revoke(_ id: UUID) throws {
        lock.lock(); defer { lock.unlock() }
        // Fail visibly if Keychain refuses deletion; never pretend to revoke.
        try secrets.remove(id)
        records.removeAll { $0.id == id }; persist()
    }
    private func persist() {
        if let data = try? JSONEncoder().encode(records) { defaults?.set(data, forKey: Self.key) }
    }
    private static func safeName(_ value: String) -> String {
        FlipperIdentity(model: "Flipper Zero", name: value, idType: 1, deviceID: Data([1]),
                        appVersion: "", protocolMajor: 1, protocolMinor: 0).displayName
    }
}
