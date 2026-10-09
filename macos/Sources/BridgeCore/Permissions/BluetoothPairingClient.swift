import Foundation

/// Runs exclusively on the BLE queue, independently of the internet session.
public final class BluetoothPairingClient {
    public static let capability: UInt32 = 1 << 5
    public static let selectionCapability: UInt32 = 1 << 6
    public static let open: UInt8 = 0x40, code: UInt8 = 0x41
    public static let needed: UInt8 = 0x42, result: UInt8 = 0x43, revoke: UInt8 = 0x44
    public static let waiting: UInt8 = 0x45
    public static let forgotten: UInt8 = 0x46
    private let store: BluetoothPairingStore
    private let peer: UUID
    private var hello: FIBPHello?
    private var pendingToken: Data?
    private var deadline = Date.distantPast
    private var awaitingReply = false
    private var awaitingCode = false
    public private(set) var awaitingFlipperSelection = false
    private var selectionSeen = false
    private var codeStarted = false
    public var expiresAt: Date { deadline }
    public private(set) var accepted = false
    public var name: String {
        guard let hello else { return "Flipper Zero" }
        return FlipperIdentity(model: hello.model, name: hello.name, idType: hello.idType,
            deviceID: hello.deviceID, appVersion: hello.appVersion, protocolMajor: 1, protocolMinor: 0).displayName
    }
    public init(store: BluetoothPairingStore, peer: UUID) { self.store = store; self.peer = peer }
    public func begin(_ frame: FIBPFrame, now: Date = Date()) throws -> FIBPFrame {
        guard hello == nil, frame.messageType == .hello, validControl(frame) else { throw BluetoothPairingError.malformed }
        let value = try FIBPHello(payload: frame.payload)
        guard value.minimumMajor <= 1, value.maximumMajor >= 1,
              (value.minimumMajor < 1 || value.minimumMinor == 0),
              value.capabilities.rawValue & (Self.capability | Self.selectionCapability) ==
                (Self.capability | Self.selectionCapability) else { throw BluetoothPairingError.incompatible }
        hello = value; deadline = now.addingTimeInterval(120); awaitingReply = true
        let saved = try store.token(for: peer, deviceID: value.deviceID) ?? Data(repeating: 0, count: 32)
        return packet(Self.open, prefix(value) + saved)
    }
    /// true requests code entry; false is waiting OR accepted. Inspect accepted
    /// before exposing the transport to the internet coordinator.
    public func receive(_ frame: FIBPFrame, now: Date = Date()) throws -> Bool {
        guard now < deadline else { throw BluetoothPairingError.timeout }
        guard let hello, !accepted, awaitingReply, validControl(frame), frame.payload.count == 9,
              frame.payload.prefix(8) == prefix(hello).prefix(8) else { throw BluetoothPairingError.malformed }
        if frame.rawMessageType == Self.waiting, frame.payload[8] == 3 {
            guard !selectionSeen else { throw BluetoothPairingError.malformed }
            selectionSeen = true; awaitingFlipperSelection = true; return false
        }
        guard selectionSeen else { throw BluetoothPairingError.rejected }
        if frame.rawMessageType == Self.needed, frame.payload[8] == 2 {
            if !codeStarted {
                guard awaitingFlipperSelection else { throw BluetoothPairingError.malformed }
                // The Flipper no longer recognizes the token (including an
                // offline Flipper-side revoke). Remove stale local recognition.
                try store.revoke(peer)
                codeStarted = true; deadline = now.addingTimeInterval(120)
            }
            awaitingFlipperSelection = false
            awaitingReply = false; awaitingCode = true; return true
        }
        guard frame.rawMessageType == Self.result, frame.payload[8] == 1 else { throw BluetoothPairingError.rejected }
        if let token = pendingToken {
            try store.establish(id: peer, name: name, deviceID: hello.deviceID, token: token)
        } else {
            guard try store.token(for: peer, deviceID: hello.deviceID) != nil else { throw BluetoothPairingError.rejected }
        }
        accepted = true; awaitingReply = false; awaitingFlipperSelection = false; pendingToken = nil
        return false
    }
    public func submit(_ digits: String, now: Date = Date()) throws -> FIBPFrame {
        guard now < deadline else { throw BluetoothPairingError.timeout }
        guard let hello, awaitingCode, digits.utf8.count == 6,
              digits.utf8.allSatisfy({ (48...57).contains($0) }) else { throw BluetoothPairingError.malformed }
        let token = BluetoothPairingStore.randomBytes(32)
        pendingToken = token; awaitingCode = false; awaitingReply = true
        return packet(Self.code, prefix(hello) + Data(digits.utf8) + token)
    }
    public func revokePacket() -> FIBPFrame? {
        guard accepted, let hello else { return nil }
        return packet(Self.revoke, prefix(hello))
    }
    public func isRevocationNotice(_ frame: FIBPFrame) -> Bool {
        guard let hello else { return false }
        return validControl(frame) && frame.rawMessageType == Self.forgotten &&
            frame.payload == prefix(hello)
    }
    private func prefix(_ hello: FIBPHello) -> Data {
        var bytes = [UInt8](); bytes.appendLittleEndian(hello.clientNonce)
        return Data(bytes) + store.hostID
    }
    private func validControl(_ frame: FIBPFrame) -> Bool {
        frame.major == 1 && frame.minor == 0 && frame.requestID == 0 && frame.sequence == 0 && frame.flags.isEmpty
    }
    private func packet(_ type: UInt8, _ data: Data) -> FIBPFrame {
        FIBPFrame(major: 1, minor: 0, rawMessageType: type, flags: [], requestID: 0, sequence: 0, payload: data)
    }
}
