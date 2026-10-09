import CoreBluetooth
import Foundation

/// Only discovery hints; authenticated GATT and bridge recognition still gate
/// all internet work. Use advertised local name, never an old OS-cached name.
public enum BluetoothAdvertisementPolicy {
    public static let uuid = "3080" // Existing firmware serial discovery marker.
    public static let maximumAdvertisingBytes = 3 + 3 + (2 + 16) + (2 + 2)
    public static func matches(services: [String], localName: String?) -> Bool {
        guard services.contains(where: { $0.uppercased() == uuid }), let localName,
              localName.hasPrefix("FZ Bridge "), (11...16).contains(localName.utf8.count),
              localName.utf8.allSatisfy({ (32...126).contains($0) }) else { return false }
        return true
    }
}

/// Opt-in, one-peer BLE alpha. CoreBluetooth owns pairing/encryption. This is
/// neither Bluetooth Classic SPP nor the Flipper firmware's RPC session.
public final class BluetoothAlphaTransport: NSObject, SerialDeviceMonitoring, SerialTransporting {
    public static let advertisementUUID = BluetoothAdvertisementPolicy.uuid
    private static let serviceUUID = CBUUID(string: "8FE5B3D5-2E7F-4A98-2A48-7ACC60FE0000")
    private static let txUUID = CBUUID(string: "19ED82AE-ED21-4C9D-4145-228E61FE0000")
    private static let rxUUID = CBUUID(string: "19ED82AE-ED21-4C9D-4145-228E62FE0000")
    private static let flowUUID = CBUUID(string: "19ED82AE-ED21-4C9D-4145-228E63FE0000")
    private static let statusUUID = CBUUID(string: "19ED82AE-ED21-4C9D-4145-228E64FE0000")

    private let queue = DispatchQueue(label: "fibp.bluetooth-alpha")
    private let lock = NSLock()
    private let diagnostics: DiagnosticsLog
    private let pairings: BluetoothPairingStore
    private var central: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var rx: CBCharacteristic?
    private var tx: CBCharacteristic?
    private var flow: CBCharacteristic?
    private var status: CBCharacteristic?
    private var device: SerialDevice?
    private var running = false
    private var announced = false
    private var openingStatus = false
    private var credits: Int?
    private var operation: WriteOperation?
    private var generation = UUID()
    private var pairingClient: BluetoothPairingClient?
    private let pairingParser = FIBPStreamParser()
    private var pairingChannelActive = false
    private var initialHello: Data?
    private var queuedPairingFrame: FIBPFrame?
    private var revoking = false
    private var pairingPrompt: ((String, @escaping (String?) -> Void) -> Void)?
    private var pairingCancelled: (() -> Void)?
    private var pairingWaiting: (() -> Void)?
    private var pairingsChanged: (() -> Void)?
    public var onPairingCodeRequired: ((String, @escaping (String?) -> Void) -> Void)? {
        get { locked { pairingPrompt } } set { locked { pairingPrompt = newValue } }
    }
    public var onPairingCancelled: (() -> Void)? {
        get { locked { pairingCancelled } } set { locked { pairingCancelled = newValue } }
    }
    public var onPairingWaitingForSelection: (() -> Void)? {
        get { locked { pairingWaiting } } set { locked { pairingWaiting = newValue } }
    }
    public var onPairingsChanged: (() -> Void)? {
        get { locked { pairingsChanged } } set { locked { pairingsChanged = newValue } }
    }

    // Only these values cross queues. All CoreBluetooth objects stay on queue.
    private var opened = false
    private var added: ((SerialDevice) -> Void)?
    private var removed: ((SerialDevice) -> Void)?
    private var received: ((Data) -> Void)?
    private var disconnected: ((Error?) -> Void)?
    public var onDeviceAdded: ((SerialDevice) -> Void)? {
        get { locked { added } } set { locked { added = newValue } }
    }
    public var onDeviceRemoved: ((SerialDevice) -> Void)? {
        get { locked { removed } } set { locked { removed = newValue } }
    }
    public var onReceive: ((Data) -> Void)? {
        get { locked { received } } set { locked { received = newValue } }
    }
    public var onDisconnect: ((Error?) -> Void)? {
        get { locked { disconnected } } set { locked { disconnected = newValue } }
    }
    public var isOpen: Bool { locked { opened } }

    public init(diagnostics: DiagnosticsLog = DiagnosticsLog(),
                pairings: BluetoothPairingStore = BluetoothPairingStore()) {
        self.diagnostics = diagnostics
        self.pairings = pairings
        super.init()
    }

    private func locked<T>(_ body: () -> T) -> T {
        lock.lock(); defer { lock.unlock() }; return body()
    }

    public func start() throws {
        queue.async {
            self.running = true
            self.diagnostics.append(.debug,
                "Bluetooth: starting CoreBluetooth manager; authorization \(CBManager.authorization.rawValue).")
            // Creating the manager is deferred until the user enables Alpha.
            if self.central == nil {
                self.central = CBCentralManager(delegate: self, queue: self.queue,
                    options: [CBCentralManagerOptionShowPowerAlertKey: false])
            } else { self.scan() }
        }
    }

    public func stop() {
        queue.async {
            self.running = false
            self.central?.stopScan()
            self.dropConnection(error: nil)
        }
    }

    public func open(device: SerialDevice) throws {
        var error: Error?
        queue.sync {
            guard self.device == device, self.announced, self.pairingClient?.accepted == true,
                  let hello = self.initialHello else { error = SerialTransportError.notOpen; return }
            self.locked { self.opened = true }
            let token = self.generation
            // Let coordinator.open finish setting its ordinary HELLO state.
            self.queue.asyncAfter(deadline: .now() + 0.02) {
                guard self.generation == token, self.isOpen else { return }
                self.onReceive?(hello)
                self.initialHello = nil
            }
        }
        if let error { throw error }
    }

    public func close() { queue.async { self.dropConnection(error: nil) } }

    /// Local credential deletion already forces a code on the next connection.
    /// Best-effort removal of the matching Flipper credential, never OS bonds.
    public func disconnectRevokedPeer(_ identifier: UUID) {
        queue.async {
            guard self.peripheral?.identifier == identifier else { return }
            guard let packet = self.pairingClient?.revokePacket() else {
                self.running = false; self.dropConnection(error: nil); return
            }
            self.revoking = true
            self.locked { self.opened = false }
            self.running = false
            let token = self.generation
            do { try self.sendPairing(packet) } catch { self.dropConnection(error: error); return }
            self.queue.asyncAfter(deadline: .now() + 4) {
                guard self.generation == token else { return }
                self.dropConnection(error: nil)
            }
        }
    }

    public func rescanIfIdle() {
        queue.async {
            guard self.running, self.peripheral == nil else { return }
            self.central?.stopScan()
            self.scan()
        }
    }

    public func send(_ data: Data) throws {
        guard !data.isEmpty else { return }
        guard data.count <= BridgeConfiguration.frameHeaderSize
            + BridgeConfiguration.maximumWirePayload + BridgeConfiguration.frameCRCSize else {
            throw BluetoothAlphaError.frameTooLarge
        }
        let write = WriteOperation(data)
        queue.async {
            guard self.isOpen, self.operation == nil, self.peripheral != nil else {
                write.complete(.failure(SerialTransportError.notOpen)); return
            }
            self.operation = write
            self.writeNextChunk()
        }
        // The session queue may wait, but the BLE delegate queue never does.
        // One frame in memory; no unbounded fire-and-forget TX backlog.
        guard write.finished.wait(timeout: .now() + 4) == .success else {
            queue.async {
                if self.operation === write { self.dropConnection(error: BluetoothAlphaError.writeTimeout) }
            }
            throw BluetoothAlphaError.writeTimeout
        }
        try write.result().get()
    }

    private func scan() {
        guard running, peripheral == nil, central?.state == .poweredOn else { return }
        central?.scanForPeripherals(withServices: [CBUUID(string: Self.advertisementUUID)], options: nil)
        diagnostics.append(.info, "Bluetooth: scanning for FZ Bridge advertisements.")
    }

    private func writeNextChunk() {
        guard let write = operation, !write.awaitingACK, let peripheral, let rx,
              (isOpen || pairingChannelActive),
              let credits else { return }
        if write.offset == write.data.count {
            operation = nil; write.complete(.success(()))
            if let next = queuedPairingFrame {
                queuedPairingFrame = nil
                do { try sendPairing(next) } catch { dropConnection(error: error) }
            } else { announcePairedDevice() }
            return
        }
        // Exhaust the advertised credits exactly; the peripheral renews them
        // only once its serial receive queue is drained.
        let length = min(128, peripheral.maximumWriteValueLength(for: .withResponse),
                         write.data.count - write.offset, credits)
        guard length > 0 else { return }
        self.credits = credits - length
        write.pendingLength = length
        write.awaitingACK = true
        peripheral.writeValue(write.data.subdata(in: write.offset..<(write.offset + length)),
                              for: rx, type: .withResponse)
    }

    private func announceIfReady() {
        guard !pairingChannelActive, let peripheral, let tx, tx.isNotifying,
              let flow, flow.isNotifying, credits != nil, rx != nil, let status else { return }
        pairingChannelActive = true; openingStatus = true
        // Authenticated GATT first, bridge recognition second, internet consent
        // third. No coordinator/device announcement before recognition succeeds.
        peripheral.writeValue(Data([0, 0, 0, 0]), for: status, type: .withResponse)
        schedulePairingExpiry(generation, fallback: Date().addingTimeInterval(120))
    }

    private func schedulePairingExpiry(_ token: UUID, fallback: Date) {
        let deadline = pairingClient?.expiresAt ?? fallback
        queue.asyncAfter(deadline: .now() + max(0, deadline.timeIntervalSinceNow)) {
            guard self.generation == token, !self.announced else { return }
            if let client = self.pairingClient, Date() < client.expiresAt {
                // Physical selection starts a separate code-entry deadline.
                self.schedulePairingExpiry(token, fallback: client.expiresAt)
            } else {
                self.running = false
                self.dropConnection(error: BluetoothPairingError.timeout)
            }
        }
    }

    private func announcePairedDevice() {
        guard !announced, pairingClient?.accepted == true, operation == nil,
              !openingStatus, let peripheral else { return }
        let device = SerialDevice(registryID: 1,
            calloutPath: "ble-alpha:\(peripheral.identifier.uuidString)",
            productName: "Flipper Bridge \(pairingClient?.name ?? "")", linkKind: .bluetoothAlpha)
        self.device = device
        announced = true
        onPairingsChanged?()
        onDeviceAdded?(device)
        diagnostics.append(.info, "Bluetooth: bridge pairing verified; internet consent required.")
    }

    private func sendPairing(_ frame: FIBPFrame) throws {
        guard pairingChannelActive else { throw SerialTransportError.notOpen }
        // A GATT indication can arrive before the previous write's ACK. Retain
        // at most one next auth frame rather than failing that ordering race.
        if operation != nil {
            guard queuedPairingFrame == nil else { throw BluetoothPairingError.malformed }
            queuedPairingFrame = frame; return
        }
        let write = WriteOperation(try FIBPCodec.encode(frame))
        operation = write; writeNextChunk()
        let token = generation
        queue.asyncAfter(deadline: .now() + 4) {
            guard self.generation == token, self.operation === write else { return }
            self.dropConnection(error: BluetoothAlphaError.writeTimeout)
        }
    }

    private func receivePairing(_ data: Data) {
        do {
            for event in pairingParser.feed(data) {
                guard case .frame(let frame) = event else { throw BluetoothPairingError.malformed }
                if try handleRevocation(frame) { return }
                guard let peripheral else { return }
                if pairingClient == nil {
                    diagnostics.append(.debug, "Bluetooth: HELLO received; checking bridge recognition.")
                    let client = BluetoothPairingClient(store: pairings, peer: peripheral.identifier)
                    pairingClient = client
                    initialHello = try FIBPCodec.encode(frame)
                    try sendPairing(client.begin(frame))
                } else if let client = pairingClient {
                    if try client.receive(frame) {
                        onPairingsChanged?()
                        diagnostics.append(.info, "Bluetooth: bridge pairing code required on Flipper.")
                        let token = generation
                        guard let prompt = onPairingCodeRequired else { throw BluetoothPairingError.rejected }
                        prompt(client.name) { [weak self, weak client] code in
                            guard let self else { return }
                            self.queue.async {
                                guard self.generation == token, let client, self.pairingClient === client else { return }
                                guard let code else {
                                    self.running = false // no immediate modal retry loop
                                    self.dropConnection(error: nil); return
                                }
                                do { try self.sendPairing(client.submit(code)) }
                                catch { self.running = false; self.dropConnection(error: error) }
                            }
                        }
                    } else if client.awaitingFlipperSelection {
                        diagnostics.append(.info, "Bluetooth: waiting for Pair/Connect on Flipper.")
                        onPairingWaitingForSelection?()
                    } else { announcePairedDevice() }
                }
            }
        } catch { running = false; dropConnection(error: error) }
    }

    private func handleRevocation(_ frame: FIBPFrame) throws -> Bool {
        guard pairingClient?.isRevocationNotice(frame) == true else { return false }
        if let peer = peripheral { try pairings.revoke(peer.identifier) }
        onPairingsChanged?()
        running = false
        diagnostics.append(.info, "Bridge pairing removed by Flipper; internet disconnected.")
        dropConnection(error: nil)
        return true
    }

    private func receiveOpenData(_ data: Data) {
        do {
            for event in pairingParser.feed(data) {
                guard case .frame(let frame) = event else { throw BluetoothPairingError.malformed }
                if try handleRevocation(frame) { return }
                onReceive?(try FIBPCodec.encode(frame))
            }
        } catch { dropConnection(error: error) }
    }

    private func dropConnection(error: Error?) {
        generation = UUID()
        let wasOpen = locked { () -> Bool in
            let was = opened; opened = false; return was
        }
        let oldDevice = device
        let peer = peripheral
        peripheral = nil; device = nil
        rx = nil; tx = nil; flow = nil; status = nil; credits = nil
        announced = false; openingStatus = false
        pairingChannelActive = false; pairingClient = nil; initialHello = nil
        queuedPairingFrame = nil
        revoking = false
        pairingParser.reset()
        onPairingCancelled?()
        operation?.complete(.failure(error ?? SerialTransportError.notOpen))
        operation = nil
        peer?.delegate = nil
        if let peer { central?.cancelPeripheralConnection(peer) }
        if let oldDevice { onDeviceRemoved?(oldDevice) }
        if wasOpen { onDisconnect?(error) }
        if let error { diagnostics.append(.warning, "Bluetooth: \(error.localizedDescription)") }
        let token = generation
        queue.asyncAfter(deadline: .now() + 2) {
            guard self.generation == token else { return }
            self.scan()
        }
    }

    private final class WriteOperation {
        let data: Data
        let finished = DispatchSemaphore(value: 0)
        var offset = 0
        var pendingLength = 0
        var awaitingACK = false
        private let lock = NSLock()
        private var outcome: Result<Void, Error>?
        init(_ data: Data) { self.data = data }
        func complete(_ result: Result<Void, Error>) {
            lock.lock()
            guard outcome == nil else { lock.unlock(); return }
            outcome = result
            lock.unlock(); finished.signal()
        }
        func result() -> Result<Void, Error> {
            lock.lock(); defer { lock.unlock() }
            return outcome ?? .failure(SerialTransportError.notOpen)
        }
    }
}

extension BluetoothAlphaTransport: CBCentralManagerDelegate, CBPeripheralDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard running else { return }
        diagnostics.append(.debug, "Bluetooth: adapter state \(central.state.rawValue).")
        switch central.state {
        case .poweredOn: scan()
        case .unauthorized:
            diagnostics.append(.warning, "Bluetooth permission denied. Enable it in macOS Privacy & Security.")
            dropConnection(error: BluetoothAlphaError.unavailable)
        case .poweredOff:
            diagnostics.append(.warning, "Bluetooth: Mac Bluetooth is off.")
            dropConnection(error: BluetoothAlphaError.unavailable)
        default: dropConnection(error: BluetoothAlphaError.unavailable)
        }
    }

    public func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                              advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard running, self.peripheral == nil else { return }
        let services = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []).map(\.uuidString)
        guard BluetoothAdvertisementPolicy.matches(services: services,
            localName: advertisementData[CBAdvertisementDataLocalNameKey] as? String) else { return }
        self.peripheral = peripheral
        diagnostics.append(.info, "Bluetooth: bridge advertisement found; connecting.")
        peripheral.delegate = self
        central.stopScan()
        central.connect(peripheral, options: nil)
        let token = generation
        queue.asyncAfter(deadline: .now() + 60) {
            guard self.generation == token, !self.pairingChannelActive else { return }
            self.dropConnection(error: BluetoothAlphaError.discoveryTimeout)
        }
    }

    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard self.peripheral === peripheral, running else {
            if self.peripheral === peripheral { dropConnection(error: nil) }
            else { central.cancelPeripheralConnection(peripheral) }
            return
        }
        diagnostics.append(.debug, "Bluetooth: BLE connected; discovering GATT service.")
        peripheral.discoverServices([Self.serviceUUID])
    }

    public func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        if self.peripheral === peripheral { dropConnection(error: error ?? BluetoothAlphaError.unavailable) }
    }

    public func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        if self.peripheral === peripheral { dropConnection(error: error) }
    }

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard self.peripheral === peripheral else { return }
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            dropConnection(error: error ?? BluetoothAlphaError.missingService); return
        }
        diagnostics.append(.debug, "Bluetooth: GATT service found; discovering characteristics.")
        peripheral.discoverCharacteristics([Self.rxUUID, Self.txUUID, Self.flowUUID, Self.statusUUID], for: service)
    }

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard self.peripheral === peripheral else { return }
        guard error == nil else { dropConnection(error: error); return }
        for characteristic in service.characteristics ?? [] {
            switch characteristic.uuid {
            case Self.rxUUID: rx = characteristic
            case Self.txUUID: tx = characteristic
            case Self.flowUUID: flow = characteristic
            case Self.statusUUID: status = characteristic
            default: break
            }
        }
        guard let tx, let flow, let rx, let status,
              tx.properties.contains(.indicate), flow.properties.contains(.notify),
              rx.properties.contains(.write), status.properties.contains(.write) else {
            dropConnection(error: BluetoothAlphaError.missingService); return
        }
        peripheral.setNotifyValue(true, for: tx)
        peripheral.setNotifyValue(true, for: flow)
        peripheral.readValue(for: flow) // Authenticated read triggers system pairing if needed.
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard self.peripheral === peripheral else { return }
        if let error { dropConnection(error: error); return }
        guard characteristic.isNotifying else { dropConnection(error: BluetoothAlphaError.unavailable); return }
        announceIfReady()
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard self.peripheral === peripheral else { return }
        if let error { dropConnection(error: error); return }
        guard let data = characteristic.value else { return }
        if characteristic.uuid == Self.flowUUID {
            guard data.count == 4 else { dropConnection(error: BluetoothAlphaError.invalidCredits); return }
            let count = data.reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
            guard count > 0, count <= 2048 else { dropConnection(error: BluetoothAlphaError.invalidCredits); return }
            credits = Int(count)
            announceIfReady()
            writeNextChunk()
        } else if characteristic.uuid == Self.txUUID {
            if revoking { return }
            if isOpen { receiveOpenData(data) }
            else if pairingChannelActive { receivePairing(data) }
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard self.peripheral === peripheral else { return }
        if let error { dropConnection(error: error); return }
        if characteristic.uuid == Self.statusUUID {
            openingStatus = false
            announcePairedDevice()
        } else if characteristic.uuid == Self.rxUUID, let write = operation, write.awaitingACK {
            write.offset += write.pendingLength
            write.awaitingACK = false
            writeNextChunk()
        }
    }
}

private enum BluetoothAlphaError: LocalizedError {
    case unavailable, missingService, discoveryTimeout, writeTimeout, frameTooLarge, invalidCredits
    var errorDescription: String? {
        switch self {
        case .unavailable: return "Bluetooth is unavailable or disconnected."
        case .missingService: return "The private FIBP GATT service is incomplete."
        case .discoveryTimeout: return "Bluetooth pairing/discovery timed out."
        case .writeTimeout: return "Bluetooth frame delivery timed out."
        case .frameTooLarge: return "Bluetooth frame exceeds the bounded TX buffer."
        case .invalidCredits: return "Bluetooth receive credits are invalid."
        }
    }
}
