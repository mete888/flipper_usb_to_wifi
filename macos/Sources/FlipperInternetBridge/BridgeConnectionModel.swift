import AppKit
import BridgeCore
import SwiftUI

/// Owns one connection. UI selection never changes the other connection's lifetime.
@MainActor
final class BridgeConnectionModel: ObservableObject {
    let kind: BridgeLinkKind
    let diagnostics: DiagnosticsLog
    private let pairingStore: BluetoothPairingStore
    private let pairingPrompter = BluetoothPairingPrompter()
    private var coordinator: BridgeCoordinator?
    private var bluetoothTransport: BluetoothAlphaTransport?
    private var generation = UUID()
    var onPairingsChanged: (() -> Void)?

    @Published private(set) var enabled = false
    @Published private(set) var phase = "Off"
    @Published private(set) var startupError: String?
    @Published private(set) var snapshot = BridgeStatusSnapshot(
        state: .disconnected, statusText: "Not connected", device: nil,
        identity: nil, permission: nil, activeRequestID: nil)

    init(kind: BridgeLinkKind, diagnostics: DiagnosticsLog, pairings: BluetoothPairingStore) {
        self.kind = kind
        self.diagnostics = diagnostics
        pairingStore = pairings
    }
    var isBluetooth: Bool { kind == .bluetoothAlpha }
    var name: String { isBluetooth ? "Bluetooth Internet Bridge" : "USB Internet Bridge" }
    var connectionText: String {
        if !enabled { return "Bridge disabled" }
        return snapshot.isConnected ? "Flipper connected" : phase
    }
    var identityText: String {
        guard let identity = snapshot.identity else { return "No device connected" }
        return "\(identity.displayName) · \(identity.redactedID)"
    }
    var canGrant: Bool {
        snapshot.identity != nil && snapshot.permission != .allowedOnce && snapshot.permission != .alwaysAllowed
    }
    var canRevoke: Bool { snapshot.identity != nil && snapshot.internetAccessEnabled }
    var canCancel: Bool { snapshot.activeRequestID != nil }
    var connectedBluetoothID: UUID? {
        guard isBluetooth, let path = snapshot.device?.calloutPath,
              path.hasPrefix("ble-alpha:") else { return nil }
        return UUID(uuidString: String(path.dropFirst("ble-alpha:".count)))
    }
    func setEnabled(_ value: Bool) { if value { start() } else { stop() } }
    func reconnect() { stop(); start() }
    func allow() { coordinator?.grantAlwaysForCurrentDevice() }
    func revoke() { coordinator?.revokeCurrentDevicePermission() }
    func cancelRequest() { coordinator?.cancelActiveRequest() }
    func revokePairing(_ id: UUID) { bluetoothTransport?.disconnectRevokedPeer(id) }

    func start() {
        guard !enabled else { return }
        enabled = true
        startupError = nil
        phase = isBluetooth ? "Searching for Flipper" : "Waiting for USB"
        let token = UUID()
        generation = token
        let monitor: any SerialDeviceMonitoring
        let transport: any SerialTransporting
        if isBluetooth {
            let ble = BluetoothAlphaTransport(diagnostics: diagnostics, pairings: pairingStore)
            bluetoothTransport = ble
            monitor = ble
            transport = ble
            ble.onPairingWaitingForSelection = { [weak self] in
                DispatchQueue.main.async {
                    guard let self, self.enabled, self.generation == token else { return }
                    self.phase = "Select this Mac in Flipper → Requests"
                }
            }
            ble.onPairingCodeRequired = { [weak self] name, completion in
                DispatchQueue.main.async {
                    guard let self, self.enabled, self.generation == token else { completion(nil); return }
                    self.phase = "Enter the code shown on Flipper"
                    self.pairingPrompter.request(name: name, completion: completion)
                }
            }
            ble.onPairingCancelled = { [weak self] in
                DispatchQueue.main.async {
                    guard let self, self.generation == token else { return }
                    self.pairingPrompter.cancel()
                    self.phase = "Disconnected; reconnect to try again"
                }
            }
            ble.onPairingsChanged = { [weak self] in
                DispatchQueue.main.async {
                    guard let self, self.generation == token else { return }
                    self.onPairingsChanged?()
                }
            }
        } else {
            #if DEBUG
            if let path = ProcessInfo.processInfo.environment["FIB_SERIAL_PORT"], !path.isEmpty {
                monitor = ManualSerialDeviceMonitor()
            } else {
                monitor = IOKitSerialDeviceMonitor()
            }
            #else
            monitor = IOKitSerialDeviceMonitor()
            #endif
            transport = POSIXSerialTransport()
        }
        let session = BridgeCoordinator(
            monitor: monitor, transport: transport,
            permissions: isBluetooth ? InMemoryPermissionStore() : UserDefaultsPermissionStore(),
            permissionPrompt: MacPermissionPrompter(bluetoothAlpha: isBluetooth),
            httpClient: HTTPSNetworkClient(policy: NetworkPolicy()), diagnostics: diagnostics,
            pongTimeout: isBluetooth ? 5 : BridgeConfiguration.pongTimeout,
            allowPersistentPermissions: !isBluetooth,
            maximumResponseBytes: isBluetooth ? BridgeConfiguration.bluetoothTextResponseBytes : BridgeConfiguration.maximumResponseBytes)
        coordinator = session
        session.onStatusChange = { [weak self] snapshot in
            DispatchQueue.main.async {
                guard let self, self.enabled, self.generation == token else { return }
                self.snapshot = snapshot
                if let id = self.connectedBluetoothID, let identity = snapshot.identity {
                    self.pairingStore.renameRecognizedPeer(id: id, name: identity.displayName)
                    self.onPairingsChanged?()
                }
            }
        }
        do {
            try session.start()
            snapshot = session.snapshot()
            #if DEBUG
            if let path = ProcessInfo.processInfo.environment["FIB_SERIAL_PORT"], !path.isEmpty,
               let manual = monitor as? ManualSerialDeviceMonitor {
                diagnostics.append(.warning, "DEBUG PTY override is enabled.")
                manual.add(SerialDevice(registryID: 1, calloutPath: path,
                    vendorID: BridgeConfiguration.flipperVendorID, productID: BridgeConfiguration.flipperProductID,
                    interfaceNumber: 2, productName: "FIBP PTY Simulator"))
            }
            #endif
        } catch {
            startupError = error.localizedDescription
            diagnostics.append(.error, "\(name): \(error.localizedDescription)")
        }
    }
    func stop() {
        generation = UUID() // Ignore already-enqueued callbacks from the old session.
        enabled = false
        pairingPrompter.cancel()
        coordinator?.onStatusChange = nil
        coordinator?.stop()
        coordinator = nil
        bluetoothTransport?.close()
        bluetoothTransport = nil
        snapshot = BridgeStatusSnapshot(state: .disconnected, statusText: "Not connected",
            device: nil, identity: nil, permission: nil, activeRequestID: nil)
        phase = "Off"
        startupError = nil
    }
}
