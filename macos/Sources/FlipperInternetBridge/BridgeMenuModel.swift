import AppKit
import BridgeCore
import Combine
import SwiftUI

@MainActor
final class BridgeMenuModel: ObservableObject {
    let usb: BridgeConnectionModel
    let bluetooth: BridgeConnectionModel
    let diagnostics: DiagnosticsLog
    private let pairingStore: BluetoothPairingStore
    private let revokePromptOwner = UUID()
    private var observations = Set<AnyCancellable>()
    private var terminationObserver: NSObjectProtocol?
    @Published var selectedLink: BridgeLinkKind = .usb
    @Published private(set) var diagnosticsEntries = [DiagnosticEntry]()
    @Published private(set) var bluetoothPairings = [BluetoothPairingRecord]()
    @Published private(set) var pairingError: String?

    init() {
        let log = DiagnosticsLog()
        let pairings = BluetoothPairingStore()
        diagnostics = log
        pairingStore = pairings
        usb = BridgeConnectionModel(kind: .usb, diagnostics: log, pairings: pairings)
        bluetooth = BridgeConnectionModel(kind: .bluetoothAlpha, diagnostics: log, pairings: pairings)
        for connection in [usb, bluetooth] {
            connection.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }
                .store(in: &observations)
        }
        bluetooth.onPairingsChanged = { [weak self] in self?.refreshPairings() }
        refreshPairings()
        let arguments = ProcessInfo.processInfo.arguments
        let stderr = arguments.contains("--diagnostics-stderr")
        log.onChange = { [weak self] entries in
            DispatchQueue.main.async {
                self?.diagnosticsEntries = entries
                if stderr, let entry = entries.last {
                    FileHandle.standardError.write(Data("[\(entry.level.rawValue)] \(entry.message)\n".utf8))
                }
            }
        }
        terminationObserver = NotificationCenter.default.addObserver(
            forName: NSApplication.willTerminateNotification, object: nil, queue: .main
        ) { [weak self] _ in MainActor.assumeIsolated { self?.stop() } }
        usb.start()
        // Legacy operator flag remains compatible; no Alpha label in the UI.
        if arguments.contains("--bluetooth") || arguments.contains("--bluetooth-alpha") {
            bluetooth.start()
            selectedLink = .bluetoothAlpha
        }
    }
    deinit {
        if let terminationObserver { NotificationCenter.default.removeObserver(terminationObserver) }
    }
    var selected: BridgeConnectionModel { selectedLink == .usb ? usb : bluetooth }
    var statusSymbol: String {
        if usb.snapshot.internetAccessEnabled || bluetooth.snapshot.internetAccessEnabled {
            return "network.badge.shield.half.filled"
        }
        return "network"
    }
    func stop() {
        MacPromptQueue.shared.cancel(owner: revokePromptOwner)
        usb.stop()
        bluetooth.stop()
    }
    private func refreshPairings() { bluetoothPairings = pairingStore.snapshot() }
    func revokePairing(_ peer: BluetoothPairingRecord) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "Revoke pairing with \(peer.name)?"
        alert.informativeText = "This removes bridge recognition and stops this device's Bluetooth connection. Pair again using a new code next time. USB permissions and macOS system bonds are unchanged."
        alert.addButton(withTitle: "Cancel")
        alert.addButton(withTitle: "Revoke Pairing")
        MacPromptQueue.shared.enqueue(owner: revokePromptOwner, run: { [weak self] in
            NSApp.activate(ignoringOtherApps: true)
            guard alert.runModal() == .alertSecondButtonReturn, let self else { return }
            do {
                try self.pairingStore.revoke(peer.id)
                self.bluetooth.revokePairing(peer.id)
                self.pairingError = nil
                self.refreshPairings()
            } catch { self.pairingError = error.localizedDescription }
        }, abort: { NSApp.abortModal(); alert.window.orderOut(nil) })
    }
}
