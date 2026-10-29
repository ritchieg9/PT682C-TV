import XCTest
@testable import SessionLifecycle

final class SessionLifecycleTests: XCTestCase {
    func testNoSecondChildBeforeActualExit() throws {
        var state = SessionLifecycle()
        let first = try XCTUnwrap(state.begin(.play))
        XCTAssertNil(state.begin(.scan))
        XCTAssertEqual(state.phase, .starting)
        XCTAssertTrue(state.markReady(first))
        state.stop(next: .scan)
        XCTAssertEqual(state.phase, .stopping)
        XCTAssertNil(state.begin(.scan))
        XCTAssertEqual(state.finish(first, code: 0), .scan)
        XCTAssertNotNil(state.begin(.scan))
    }
    func testFirmwareEvidenceSurvivesCloseAndFailure() throws {
        var state = SessionLifecycle()
        let first = try XCTUnwrap(state.begin(.play))
        state.verifyFirmware(first)
        state.stop(next: nil)
        XCTAssertNil(state.finish(first, code: 0))
        XCTAssertTrue(state.firmwareWasVerified)
        let second = try XCTUnwrap(state.begin(.play))
        XCTAssertEqual(state.phase, .starting) // Not ready merely because it spawned.
        XCTAssertNil(state.finish(second, code: 1))
        XCTAssertTrue(state.firmwareWasVerified)
    }
    func testStaleEventsCannotAffectNewChild() throws {
        var state = SessionLifecycle()
        let old = try XCTUnwrap(state.begin(.play))
        _ = state.finish(old, code: 0)
        let current = try XCTUnwrap(state.begin(.scan))
        XCTAssertFalse(state.markReady(old))
        state.verifyFirmware(old)
        XCTAssertFalse(state.firmwareWasVerified)
        XCTAssertNil(state.finish(old, code: 1))
        XCTAssertEqual(state.generation, current)
        XCTAssertEqual(state.phase, .starting)
    }
    func testFailedShutdownDoesNotLaunchQueuedScan() throws {
        var state = SessionLifecycle()
        let token = try XCTUnwrap(state.begin(.play))
        state.stop(next: .scan)
        XCTAssertNil(state.finish(token, code: -1))
        XCTAssertEqual(state.phase, .failed)
        XCTAssertNil(state.pending)
    }
    func testReadinessCannotUndoStopping() throws {
        var state = SessionLifecycle()
        let token = try XCTUnwrap(state.begin(.play))
        state.stop(next: nil)
        XCTAssertFalse(state.markReady(token))
        XCTAssertEqual(state.phase, .stopping)
    }
}
