import AppKit

/// A bridge code, not an attempt to reset the OS Bluetooth PIN/bond.
@MainActor
final class BluetoothPairingPrompter {
    private var active: NSAlert?
    private var generation = UUID()
    private let owner = UUID()
    func cancel() {
        generation = UUID()
        MacPromptQueue.shared.cancel(owner: owner)
    }
    func request(name: String, completion: @escaping (String?) -> Void) {
        cancel()
        let token = generation
        let alert = NSAlert()
        alert.messageText = "Pair Flipper Zero \(name)"
        alert.informativeText = "Enter the six-digit bridge pairing code shown on your Flipper. Pairing does not grant internet access."
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 240, height: 28))
        field.placeholderString = "6-digit code"
        field.font = .monospacedDigitSystemFont(ofSize: 20, weight: .medium)
        alert.accessoryView = field
        alert.addButton(withTitle: "Cancel")
        alert.addButton(withTitle: "Pair")
        alert.buttons[0].keyEquivalent = "\u{1b}"
        alert.buttons[1].keyEquivalent = "\r"
        alert.window.initialFirstResponder = field
        MacPromptQueue.shared.enqueue(owner: owner, run: { [weak self] in
          guard let self, self.generation == token else { return }
          self.active = alert
          NSApp.activate(ignoringOtherApps: true)
          while self.generation == token {
            let result = alert.runModal()
            guard self.generation == token else { return }
            if result != .alertSecondButtonReturn {
                self.active = nil; completion(nil); return
            }
            let code = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
            if code.utf8.count == 6 && code.utf8.allSatisfy({ (48...57).contains($0) }) {
                self.active = nil; completion(code); return
            }
            alert.informativeText = "Use exactly six digits from the code displayed on your Flipper."
          }
        }, abort: { [weak self] in
            guard let self, let active = self.active else { return }
            NSApp.abortModal()
            active.window.orderOut(nil)
            self.active = nil
        })
    }
}
