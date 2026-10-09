import Foundation
import XCTest
@testable import BridgeCore

/// Explicit operator-only hardware QA. Never run in CI or with another helper
/// open. Uses an in-memory Allow Once; no Keychain or permanent grants change.
final class RadioUSBHardwareTests: XCTestCase {
    func testOptInRealUSBStreamingAndPhysicalBack() throws {
        guard ProcessInfo.processInfo.environment["FIB_USB_RADIO_QA"] == "1" else {
            throw XCTSkip("Requires a connected Flipper, operator consent and physical Play/Back.")
        }
        let transport = ObservedUSBTransport()
        let coordinator = BridgeCoordinator(monitor: IOKitSerialDeviceMonitor(),
            transport: transport, permissions: InMemoryPermissionStore(),
            permissionPrompt: HardwareOncePrompt(), httpClient: HTTPSNetworkClient(policy: NetworkPolicy()),
            allowPersistentPermissions: false)
        try coordinator.start()
        defer { coordinator.stop() }
        print("USB QA: waiting for operator to select a USB radio station, then physical Back.")
        // Includes mode selection and manual country entry, not just playback.
        let deadline = Date().addingTimeInterval(300)
        var firstAudio: Date?
        var lastReport = Date.distantPast
        var totalAtBack: Int?
        while Date() < deadline {
            let stats = transport.statistics()
            if stats.bytes > 0 && firstAudio == nil { firstAudio = Date() }
            if Date().timeIntervalSince(lastReport) >= 10 {
                print("USB QA: PCM bytes=\(stats.bytes), wire errors=\(stats.errors), state=\(coordinator.snapshot().state)")
                lastReport = Date()
            }
            if stats.cancel && firstAudio != nil {
                totalAtBack = stats.bytes
                break
            }
            Thread.sleep(forTimeInterval: 0.1)
        }
        XCTAssertNotNil(firstAudio, "No PCM reached the real USB device")
        XCTAssertNotNil(totalAtBack, "Operator must press physical Back after streaming")
        guard let firstAudio, let totalAtBack else { return }
        XCTAssertGreaterThan(Date().timeIntervalSince(firstAudio), 30)
        XCTAssertGreaterThan(totalAtBack, 800_000)
        XCTAssertEqual(transport.statistics().errors, 0)
        XCTAssertEqual(transport.statistics().starts, 1, "A healthy station must not keep reconnecting")
        XCTAssertGreaterThan(Double(totalAtBack) / Date().timeIntervalSince(firstAudio), 25_000,
                             "USB must keep up with the 28,986 B/s speaker")
        Thread.sleep(forTimeInterval: 2)
        // Already accepted kernel bytes can arrive around Cancel. There must
        // not be a continuing source or a large queued audio tail afterward.
        XCTAssertLessThanOrEqual(transport.statistics().bytes - totalAtBack, 2048)
        XCTAssertNil(coordinator.snapshot().activeRequestID)
        print("USB QA measurements complete; see XCTest assertions for the result.")
    }
}

private final class HardwareOncePrompt: PermissionPrompting {
    func requestPermission(for identity: FlipperIdentity,
                           completion: @escaping (BridgePermissionDecision) -> Void) {
        completion(.allowOnce)
    }
    func cancelPendingPrompt() {}
}

private final class ObservedUSBTransport: SerialTransporting {
    private let base = POSIXSerialTransport()
    private let lock = NSLock()
    private let parser = FIBPStreamParser()
    private var bytes = 0
    private var errors = 0
    private var cancel = false
    private var starts = 0
    private var pcmIDs = Set<UInt32>()
    var onReceive: ((Data) -> Void)?
    var onDisconnect: ((Error?) -> Void)?
    var isOpen: Bool { base.isOpen }
    init() {
        base.onReceive = { [weak self] data in
            guard let self else { return }
            self.lock.lock()
            for event in self.parser.feed(data) {
                if case let .frame(frame) = event {
                    if frame.messageType == .cancel { self.cancel = true }
                    if frame.messageType == .error { self.errors += 1 }
                }
            }
            self.lock.unlock()
            self.onReceive?(data)
        }
        base.onDisconnect = { [weak self] error in self?.onDisconnect?(error) }
    }
    func open(device: SerialDevice) throws { try base.open(device: device) }
    func send(_ data: Data) throws {
        try base.send(data)
        lock.lock(); defer { lock.unlock() }
        for event in FIBPStreamParser().feed(data) {
            if case let .frame(frame) = event {
                if frame.messageType == .responseHeader,
                   let header = try? BridgeHTTPHeader(payload: frame.payload),
                   header.value == "audio/x-fib-pcm;rate=14493;channels=1;format=s16le",
                   pcmIDs.insert(frame.requestID).inserted { starts += 1 }
                if frame.messageType == .responseBodyChunk && pcmIDs.contains(frame.requestID) {
                    bytes += frame.payload.count
                }
                if frame.messageType == .error { errors += 1 }
            }
        }
    }
    func close() { base.close() }
    func statistics() -> (bytes: Int, errors: Int, cancel: Bool, starts: Int) {
        lock.lock(); defer { lock.unlock() }
        return (bytes, errors, cancel, starts)
    }
}
