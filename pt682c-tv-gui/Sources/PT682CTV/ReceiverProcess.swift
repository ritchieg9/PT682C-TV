import Foundation
import Darwin

/// Owns one receiver until output has reached EOF AND the process has exited.
/// All callbacks and writes run on MainActor; one background reader owns stdout.
@MainActor
final class ReceiverProcess {
    private var process: Process?
    private var input: FileHandle?
    private var stopRequested = false
    private var finished = false

    var onLine: ((String) -> Void)?
    var onExit: ((Int32) -> Void)?

    func launch(binary: URL, firmware: URL, arguments: [String],
                workDir: URL, vlcNeutral: URL) throws {
        let child = Process()
        let stdin = Pipe()
        let stdout = Pipe()
        child.executableURL = binary
        child.currentDirectoryURL = workDir
        child.arguments = [firmware.path] + arguments
        var environment = ProcessInfo.processInfo.environment
        environment["VLC_BIN"] = vlcNeutral.path
        child.environment = environment
        child.standardInput = stdin
        child.standardOutput = stdout
        child.standardError = stdout
        // A child may exit just before a command is written. Report EPIPE,
        // rather than allowing SIGPIPE to terminate the entire GUI.
        signal(SIGPIPE, SIG_IGN)
        try child.run()
        try? stdout.fileHandleForWriting.close()
        try? stdin.fileHandleForReading.close()
        process = child
        input = stdin.fileHandleForWriting

        let output = stdout.fileHandleForReading
        onLine?("Lector de salida 0.3.1: lectura POSIX por bloques disponibles, sin esperar 4096 bytes.")
        DispatchQueue.global(qos: .userInitiated).async {
            var pending = Data()
            var bytes = [UInt8](repeating: 0, count: 4096)
            func deliver(_ bytes: Data) {
                let text = String(decoding: bytes, as: UTF8.self)
                    .trimmingCharacters(in: .newlines)
                guard !text.isEmpty else { return }
                DispatchQueue.main.async { self.onLine?(text) }
            }
            while true {
                // FileHandle.read(upToCount:) may accumulate reads toward the
                // requested count. Here a menu/prompt can be shorter than that
                // count and the receiver then waits for a GUI command: deadlock.
                // POSIX read returns the bytes currently available in the pipe.
                let count = bytes.withUnsafeMutableBytes { buffer in
                    Darwin.read(output.fileDescriptor, buffer.baseAddress!, buffer.count)
                }
                if count == 0 { break }
                if count < 0 {
                    let code = errno
                    if code == EINTR { continue }
                    deliver(Data("Error leyendo receptor (errno \(code)): \(String(cString: strerror(code)))\n".utf8))
                    break
                }
                pending.append(contentsOf: bytes.prefix(count))
                while let newline = pending.firstIndex(of: 10) {
                    deliver(Data(pending[..<newline]))
                    pending.removeSubrange(...newline)
                }
                // The v0.4 save prompt is flushed without a trailing newline.
                if let text = String(data: pending, encoding: .utf8),
                   text.contains("Guardar estos"),
                   text.contains("conservar la lista anterior: ") {
                    deliver(pending)
                    pending.removeAll(keepingCapacity: true)
                }
            }
            if !pending.isEmpty { deliver(pending) }
            child.waitUntilExit()
            try? output.close()
            let status = child.terminationReason == .exit ? child.terminationStatus : -1
            // Enqueued after all lines on the same serial queue.
            DispatchQueue.main.async {
                self.finished = true
                try? self.input?.close()
                self.input = nil
                self.onExit?(status)
            }
        }
    }

    @discardableResult
    func send(_ text: String) -> Bool {
        guard !finished, let child = process, child.isRunning, let input else { return false }
        do {
            try input.write(contentsOf: Data((text + "\n").utf8))
            return true
        } catch {
            onLine?("Error enviando comando: \(error)")
            return false
        }
    }

    func requestStop(interrupt: Bool) {
        guard !stopRequested, !finished, let child = process else { return }
        stopRequested = true
        if child.isRunning {
            if interrupt { child.interrupt() }
            else { send("q") }
        }
        // Normal exit must finish first. Only a stuck child receives signals.
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) {
            guard !self.finished, child.isRunning else { return }
            self.onLine?("Cierre lento: enviando SIGTERM solo al receptor hijo.")
            child.terminate()
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                guard !self.finished, child.isRunning else { return }
                self.onLine?("Receptor sin respuesta: SIGKILL. Recuperacion USB no garantizada.")
                kill(child.processIdentifier, SIGKILL)
            }
        }
    }
}
