import AppKit
import BridgeCore
import Foundation

final class MacPermissionPrompter: PermissionPrompting {
    private var activeAlert: NSAlert?
    private var promptToken: UUID?
    private let bluetoothAlpha: Bool
    private let owner = UUID()

    init(bluetoothAlpha: Bool = false) { self.bluetoothAlpha = bluetoothAlpha }

    func requestPermission(
        for identity: FlipperIdentity,
        completion: @escaping (BridgePermissionDecision) -> Void
    ) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.cancelOnMain()

            let alert = NSAlert()
            alert.alertStyle = .informational
            alert.messageText = "Flipper Zero is requesting internet access"
            alert.informativeText = "The Flipper Zero named \(identity.displayName) wants to send HTTPS requests through this Mac's internet connection. Your Wi-Fi password is never shared. This application performs only the internet requests sent by the Flipper."
            if self.bluetoothAlpha {
                alert.messageText = "Bluetooth Internet Bridge is requesting internet access"
                alert.informativeText += " Bluetooth permissions last only for this connection. Pairing identifies the connection; it does not grant internet access."
            }
            alert.addButton(withTitle: "Deny")
            alert.addButton(withTitle: "Allow Once")
            if !self.bluetoothAlpha { alert.addButton(withTitle: "Always Allow") }
            alert.buttons[0].keyEquivalent = "\r"
            alert.buttons[1].keyEquivalent = ""
            if alert.buttons.count > 2 { alert.buttons[2].keyEquivalent = "" }
            let token = UUID()
            self.promptToken = token
            MacPromptQueue.shared.enqueue(owner: self.owner, run: { [weak self] in
                guard let self, self.promptToken == token else { return }
                self.activeAlert = alert
                NSApp.activate(ignoringOtherApps: true)
                let response = alert.runModal()
                guard self.promptToken == token else { return }
                self.activeAlert = nil
                self.promptToken = nil
                switch response {
                case .alertSecondButtonReturn: completion(.allowOnce)
                case .alertThirdButtonReturn: completion(.alwaysAllow)
                default: completion(.deny)
                }
            }, abort: { [weak self] in
                guard let self, let active = self.activeAlert else { return }
                NSApp.abortModal()
                active.window.orderOut(nil)
                self.activeAlert = nil
            })
        }
    }

    func cancelPendingPrompt() {
        // Keep the prompter alive until its alert is dismissed, even when its
        // coordinator is released immediately after stop().
        DispatchQueue.main.async { self.cancelOnMain() }
    }

    @MainActor private func cancelOnMain() {
        promptToken = nil
        MacPromptQueue.shared.cancel(owner: owner)
    }
}
