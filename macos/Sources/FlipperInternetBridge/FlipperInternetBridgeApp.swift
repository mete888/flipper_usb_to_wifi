import AppKit
import BridgeCore
import SwiftUI

final class BridgeAppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
    }
}

@main
struct FlipperInternetBridgeApp: App {
    @NSApplicationDelegateAdaptor(BridgeAppDelegate.self) private var appDelegate
    @StateObject private var model = BridgeMenuModel()
    var body: some Scene {
        MenuBarExtra {
            BridgeMenu(model: model)
        } label: {
            Label("Flipper Internet Bridge", systemImage: model.statusSymbol)
        }
        .menuBarExtraStyle(.window)
        Window("Bluetooth Pairings", id: "bluetooth") {
            BluetoothManagementView(model: model)
        }
        .defaultSize(width: 420, height: 300)
        Window("Flipper Internet Bridge Diagnostics", id: "diagnostics") {
            DiagnosticsView(model: model)
        }
        .defaultSize(width: 680, height: 420)
    }
}

private struct BridgeMenu: View {
    @ObservedObject var model: BridgeMenuModel
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Flipper Internet Bridge").font(.headline)
                Spacer()
                Image(systemName: model.statusSymbol).foregroundStyle(.secondary)
            }
            Picker("Connection", selection: $model.selectedLink) {
                Text("USB").tag(BridgeLinkKind.usb)
                Text("Bluetooth").tag(BridgeLinkKind.bluetoothAlpha)
            }
            .pickerStyle(.segmented)
            ConnectionPanel(connection: model.selected, showPairings: {
                NSApp.activate(ignoringOtherApps: true)
                openWindow(id: "bluetooth")
            })
            Divider()
            HStack {
                Button("Diagnostics") {
                    NSApp.activate(ignoringOtherApps: true)
                    openWindow(id: "diagnostics")
                }
                Spacer()
                Button("Quit") { model.stop(); NSApp.terminate(nil) }.keyboardShortcut("q")
            }
        }
        .padding(18)
        .frame(width: 340)
    }
}

private struct ConnectionPanel: View {
    @ObservedObject var connection: BridgeConnectionModel
    let showPairings: () -> Void
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Toggle(connection.name, isOn: Binding(
                get: { connection.enabled }, set: { connection.setEnabled($0) }))
                .toggleStyle(.switch).font(.subheadline.weight(.medium))
            VStack(alignment: .leading, spacing: 5) {
                HStack(spacing: 7) {
                    Circle().fill(connection.snapshot.internetAccessEnabled ? Color.green : Color.secondary)
                        .frame(width: 6, height: 6)
                    Text(connection.connectionText).font(.subheadline)
                }
                Text(connection.identityText).font(.caption).foregroundStyle(.secondary)
                Text(connection.snapshot.internetAccessEnabled ? "Internet access ready" : connection.snapshot.statusText)
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            if let error = connection.startupError {
                Text(error).font(.caption).foregroundStyle(.red)
            }
            HStack {
                Button(connection.isBluetooth ? "Allow Once" : "Always Allow") { connection.allow() }
                    .disabled(!connection.canGrant)
                Button("Revoke Access") { connection.revoke() }.disabled(!connection.canRevoke)
            }
            if connection.canCancel {
                Button("Cancel Request") { connection.cancelRequest() }
            }
            if connection.isBluetooth {
                HStack {
                    Button("Pairings…", action: showPairings)
                    Spacer()
                    Button("Reconnect") { connection.reconnect() }.disabled(!connection.enabled)
                }
                Text("Select this computer in Flipper → Requests. Pairing and internet permission are separate.")
                    .font(.caption).foregroundStyle(.secondary)
            } else {
                Text("Connect by USB, then choose USB Internet Bridge on Flipper. Internet Radio uses this connection.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}

private struct BluetoothManagementView: View {
    @ObservedObject var model: BridgeMenuModel
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Flippers recognized by this bridge").font(.headline)
            Text("Revoke Pairing forgets bridge recognition, not USB internet permission or macOS system bonds.")
                .font(.caption).foregroundStyle(.secondary)
            if model.bluetoothPairings.isEmpty {
                Spacer()
                Text("No recognized Flippers yet. Choose Bluetooth Internet Bridge → Requests on Flipper and pair with this computer.")
                    .foregroundStyle(.secondary)
                Spacer()
            } else {
                List(model.bluetoothPairings) { peer in
                    HStack(spacing: 12) {
                        VStack(alignment: .leading, spacing: 3) {
                            Text(peer.name).font(.headline)
                            Text("ID …\(peer.id.uuidString.suffix(8))")
                                .font(.caption.monospaced()).foregroundStyle(.secondary)
                            Text(model.bluetooth.connectedBluetoothID == peer.id ? "Connected" : "Paired with bridge")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        Button("Revoke Pairing…") { model.revokePairing(peer) }
                    }.padding(.vertical, 5)
                }
            }
            if let error = model.pairingError { Text(error).font(.caption).foregroundStyle(.red) }
        }
        .padding(20).frame(minWidth: 380, minHeight: 260)
    }
}

private struct DiagnosticsView: View {
    @ObservedObject var model: BridgeMenuModel

    private static let dateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .none
        formatter.timeStyle = .medium
        return formatter
    }()

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Diagnostics")
                .font(.title2)
            Text("URL queries, bodies, header values, and full device IDs are not logged.")
                .font(.caption)
                .foregroundStyle(.secondary)
            List(model.diagnosticsEntries) { entry in
                HStack(alignment: .top, spacing: 10) {
                    Text(Self.dateFormatter.string(from: entry.timestamp))
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                    Text(entry.level.rawValue.uppercased())
                        .font(.caption.monospaced())
                        .frame(width: 62, alignment: .leading)
                    Text(entry.message)
                        .textSelection(.enabled)
                }
            }
        }
        .padding()
    }
}
