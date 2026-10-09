import XCTest
@testable import BridgeCore

final class OwnedPromptQueueTests: XCTestCase {
    @MainActor func testCancellationIsOwnerScopedAndPendingReplacementIsBounded() async {
        let queue = OwnedPromptQueue()
        let usb = UUID(), bluetooth = UUID()
        let finished = expectation(description: "prompts drained")
        var events = [String]()
        queue.enqueue(owner: usb, run: {
            events.append("usb")
            queue.cancel(owner: bluetooth)
            queue.enqueue(owner: bluetooth, run: {
                events.append("replacement")
                queue.cancel(owner: bluetooth)
                finished.fulfill()
            }, abort: { events.append("bluetooth-aborted") })
        }, abort: { events.append("usb-aborted") })
        queue.enqueue(owner: bluetooth, run: { events.append("stale") },
            abort: { events.append("stale-aborted") })
        await fulfillment(of: [finished], timeout: 2)
        XCTAssertEqual(events, ["usb", "replacement", "bluetooth-aborted"])
    }

    @MainActor func testPendingReplacementAndCancellationDoNotPresentRemovedPrompt() async {
        let queue = OwnedPromptQueue()
        let owner = UUID(), other = UUID()
        let finished = expectation(description: "remaining prompt")
        var events = [Int]()
        queue.enqueue(owner: owner, run: { events.append(1) }, abort: { XCTFail("not running") })
        queue.enqueue(owner: owner, run: { events.append(2) }, abort: { XCTFail("not running") })
        queue.cancel(owner: owner)
        queue.enqueue(owner: other, run: { events.append(3); finished.fulfill() }, abort: {})
        await fulfillment(of: [finished], timeout: 2)
        XCTAssertEqual(events, [3])
    }
}
