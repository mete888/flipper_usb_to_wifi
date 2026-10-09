import Foundation

/// NSApp has one modal stack. USB consent, Bluetooth consent and pairing must
/// serialize, and cancelling one channel must never abort the other's alert.
@MainActor
public final class OwnedPromptQueue {
    public static let shared = OwnedPromptQueue()
    public init() {}
    private struct Job {
        let owner: UUID
        let run: () -> Void
        let abort: () -> Void
    }
    private var waiting = [Job]()
    private var current: Job?

    public func enqueue(owner: UUID, run: @escaping () -> Void, abort: @escaping () -> Void) {
        cancel(owner: owner)
        waiting.append(Job(owner: owner, run: run, abort: abort))
        scheduleNext()
    }
    public func cancel(owner: UUID) {
        waiting.removeAll { $0.owner == owner }
        if current?.owner == owner { current?.abort() }
    }
    private func scheduleNext() {
        DispatchQueue.main.async { [weak self] in
            guard let self, self.current == nil, !self.waiting.isEmpty else { return }
            let job = self.waiting.removeFirst()
            self.current = job
            job.run()
            self.current = nil
            self.scheduleNext()
        }
    }
}
