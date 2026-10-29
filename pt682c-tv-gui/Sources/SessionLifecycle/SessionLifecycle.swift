import Foundation

/// Pure state policy. No USB calls and no automatic cold fallback.
public struct SessionLifecycle {
    public enum Phase: Equatable {
        case idle, starting, ready, stopping, failed
    }
    public enum Intent: Equatable {
        case play, scan
    }
    public private(set) var phase: Phase = .idle
    public private(set) var generation: UUID?
    public private(set) var intent: Intent?
    public private(set) var pending: Intent?
    public private(set) var firmwareWasVerified = false

    public init() {}
    public var hasChild: Bool { generation != nil }

    @discardableResult
    public mutating func begin(_ intent: Intent) -> UUID? {
        guard !hasChild else { return nil }
        let token = UUID()
        generation = token
        self.intent = intent
        phase = .starting
        return token
    }
    public mutating func verifyFirmware(_ token: UUID) {
        guard generation == token else { return }
        firmwareWasVerified = true
    }
    @discardableResult
    public mutating func markReady(_ token: UUID) -> Bool {
        guard generation == token, phase == .starting else { return false }
        phase = .ready
        intent = .play
        return true
    }
    public mutating func stop(next: Intent?) {
        pending = next
        if hasChild { phase = .stopping }
    }
    public mutating func finish(_ token: UUID, code: Int32) -> Intent? {
        guard generation == token else { return nil }
        let next = code == 0 ? pending : nil
        phase = code == 0 ? .idle : .failed
        generation = nil
        intent = nil
        pending = nil
        return next
    }
}
