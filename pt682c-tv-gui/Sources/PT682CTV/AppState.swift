import AppKit
import Foundation
import SwiftUI
import SessionLifecycle
import VLCShim
import TSRecorder
import UniformTypeIdentifiers

enum StartMode: String, CaseIterable, Identifiable {
    case warm = "Caliente"
    case cold = "Frio"
    var id: String { rawValue }
}

struct Channel: Identifiable, Hashable {
    let id: Int
    let title: String
    let frequencyHz: UInt32
    let program: UInt32
    var frequencyMHz: Double { Double(frequencyHz) / 1_000_000 }
}

struct AppConfig: Codable {
    let childCold: String
    let childWarm: String
    let firmware: String
    let channelsFile: String
    let workDir: String
    let vlcNeutral: String
    let vlcRuntime: String?
}

@MainActor
final class AppState: ObservableObject {
    @Published var channels: [Channel] = []
    @Published var selectedId: Int?
    @Published private(set) var lifecycle = SessionLifecycle()
    @Published var startMode: StartMode = .warm
    @Published var snrText = "—"
    @Published var frameLock = false
    @Published var atscLock = false
    @Published var fecLock = false
    @Published var currentTitle = "Sin canal"
    @Published var logLines: [String] = []
    @Published var volume: Double = 80 {
        didSet { vlcshim_set_volume(Int32(volume)) }
    }
    @Published var muted = false {
        didSet { vlcshim_set_mute(muted ? 1 : 0) }
    }
    @Published var errorMessage: String?
    @Published var showLog = false
    @Published var scanning = false
    @Published var scanProgress = "Escaneando..."
    @Published var scanFound = 0
    @Published var pendingScanSave = false
    @Published private(set) var tuning = false
    @Published private(set) var videoPlaying = false
    @Published private(set) var isRecording = false
    @Published private(set) var recordingURL: URL?
    @Published private(set) var recordingBytes: UInt64 = 0
    @Published private(set) var recordingSeconds = 0
    private var recordingTimer: Timer?
    private var recordingStart: Date?

    var videoNSView: NSView?
    private var receiver: ReceiverProcess?
    private var config: AppConfig?
    private var vlcReady = false
    private var expectedStop = false
    private var isQuitting = false
    private var terminationCompletion: (() -> Void)?
    private var logFile: FileHandle?
    private var lastProgram: Int32?

    var connected: Bool { lifecycle.phase == .ready }
    var hasReceiver: Bool { lifecycle.hasChild }
    var canStart: Bool { !hasReceiver && !isQuitting }
    var canChooseChannel: Bool {
        connected && !scanning && !tuning && !isQuitting && !isRecording
    }
    var canRecord: Bool {
        connected && videoPlaying && !scanning && !tuning && !isQuitting && !isRecording
    }
    var recordingSummary: String {
        let time = String(format: "%02d:%02d:%02d",
                          recordingSeconds / 3600, (recordingSeconds / 60) % 60,
                          recordingSeconds % 60)
        let size = ByteCountFormatter.string(fromByteCount: Int64(clamping: recordingBytes),
                                             countStyle: .file)
        return "\(time) · \(size)" + (isRecording && recordingBytes == 0 ? " · Esperando datos" : "")
    }
    var canRequestScan: Bool {
        !isQuitting && !scanning && !tuning && !isRecording
            && (canStart || lifecycle.phase == .ready)
    }
    var statusText: String {
        switch lifecycle.phase {
        case .idle: return "Desconectado"
        case .starting: return scanning ? "Inicializando escaneo" : "Inicializando receptor"
        case .ready: return tuning ? "Sintonizando" : "Listo"
        case .stopping: return "Cerrando receptor…"
        case .failed: return "Error de receptor"
        }
    }

    func bootstrap() {
        do {
            config = try Self.loadConfig()
        } catch {
            errorMessage = "No se pudo preparar la aplicacion: \(error.localizedDescription)"
            return
        }
        if let cfg = config {
            let logs = URL(fileURLWithPath: cfg.workDir).appendingPathComponent("logs")
            try? FileManager.default.createDirectory(at: logs, withIntermediateDirectories: true)
            let name = "gui-\(Int(Date().timeIntervalSince1970))-\(UUID().uuidString.prefix(6)).txt"
            let path = logs.appendingPathComponent(name)
            FileManager.default.createFile(atPath: path.path, contents: nil)
            logFile = try? FileHandle(forWritingTo: path)
            appendLog("PT682C GUI 0.6: app autocontenida, VLC/libusb locales y grabacion TS.")
            appendLog("Datos y diagnosticos: \(cfg.workDir)")
            loadChannels()
        } else {
            errorMessage = "Falta app-config.json. Ejecuta build-app.sh."
        }
        // A cold start is always an explicit choice after physical attachment.
        // Relaunching the GUI must never automatically repeat a firmware upload.
        startMode = .warm
    }

    static func loadConfig() throws -> AppConfig? {
        if Bundle.main.bundleURL.pathExtension == "app" {
            let base = try FileManager.default.url(
                for: .applicationSupportDirectory, in: .userDomainMask,
                appropriateFor: nil, create: true)
            return try AppStorage.prepare(bundle: Bundle.main.bundleURL,
                support: base.appendingPathComponent("PT682C TV", isDirectory: true))
        }
        var urls: [URL] = []
        if let bundled = Bundle.main.url(forResource: "app-config", withExtension: "json") {
            urls.append(bundled)
        }
        urls.append(URL(fileURLWithPath: CommandLine.arguments[0])
            .deletingLastPathComponent().appendingPathComponent("app-config.json"))
        urls.append(URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
            .appendingPathComponent("app-config.json"))
        for url in urls {
            if let data = try? Data(contentsOf: url),
               let cfg = try? JSONDecoder().decode(AppConfig.self, from: data) { return cfg }
        }
        return nil
    }

    private func parseChannels(_ text: String) throws -> [Channel] {
        var list: [Channel] = []
        for raw in text.split(separator: "\n") {
            let line = String(raw).trimmingCharacters(in: .newlines)
            if line.hasPrefix("#") || line.isEmpty { continue }
            let fields = line.split(separator: "\t", omittingEmptySubsequences: false)
            guard fields.count == 3, !fields[0].isEmpty,
                  fields[0].utf8.count < 256,
                  let hz = UInt32(fields[1]), (54_000_000...858_000_000).contains(hz),
                  let program = UInt32(fields[2]), (1...65535).contains(program),
                  list.count < 512 else {
                throw NSError(domain: "PT682CTV", code: 1,
                              userInfo: [NSLocalizedDescriptionKey: "Lista TSV invalida: \(line)"])
            }
            list.append(Channel(id: list.count + 1, title: String(fields[0]),
                                frequencyHz: hz, program: program))
        }
        return list
    }

    private func loadChannels() {
        guard let cfg = config else { return }
        do {
            let text = try String(contentsOfFile: cfg.channelsFile, encoding: .utf8)
            let list = try parseChannels(text)
            selectedId = nil
            channels = list
            appendLog("Lista cargada: \(list.count) canales.")
        } catch { errorMessage = error.localizedDescription }
    }

    func reloadChannels() {
        guard !scanning, !tuning, !isRecording, lifecycle.phase != .starting,
              lifecycle.phase != .stopping else { return }
        if hasReceiver {
            // Reload in both processes, but only AFTER the previous child exits.
            stopReceiver(next: .play)
        } else {
            loadChannels()
        }
    }

    func importChannels() {
        guard canStart, let cfg = config else { return }
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.message = "Importar TSV: nombre, frecuencia Hz, programa"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let text = try String(contentsOf: url, encoding: .utf8)
            let list = try parseChannels(text)
            guard !list.isEmpty else { throw NSError(domain: "PT682CTV", code: 2,
                userInfo: [NSLocalizedDescriptionKey: "La lista esta vacia."]) }
            let alert = NSAlert()
            alert.messageText = "¿Reemplazar la lista guardada con \(list.count) canales?"
            alert.informativeText = "Se creara un respaldo. El receptor y la interfaz usaran la misma lista."
            alert.addButton(withTitle: "Importar")
            alert.addButton(withTitle: "Cancelar")
            guard alert.runModal() == .alertFirstButtonReturn else { return }
            let target = URL(fileURLWithPath: cfg.channelsFile)
            if FileManager.default.fileExists(atPath: target.path) {
                try FileManager.default.copyItem(at: target, to: URL(fileURLWithPath:
                    target.path + ".bak-import-" + UUID().uuidString))
            }
            try Data(text.utf8).write(to: target, options: .atomic)
            loadChannels()
        } catch { errorMessage = error.localizedDescription }
    }

    // MARK: One child from spawn until confirmed termination.

    func connect() {
        if connected { stopReceiver(next: nil) }
        else if canStart { start(.play) }
    }

    private func preparePlayer() -> Bool {
        guard tsrelay_start(1234, 1235) == 0 else {
            errorMessage = recorderError()
            return false
        }
        if vlcReady { return true }
        let runtime: URL
        if Bundle.main.bundleURL.pathExtension == "app" {
            runtime = Bundle.main.bundleURL
                .appendingPathComponent("Contents/Resources/VLCRuntime")
        } else if let path = config?.vlcRuntime {
            runtime = URL(fileURLWithPath: path)
        } else {
            errorMessage = "Falta la ruta del runtime local. Ejecuta build-app.sh."
            return false
        }
        guard FileManager.default.fileExists(atPath:
            runtime.appendingPathComponent("runtime-manifest.json").path) else {
            errorMessage = "Falta VLC local en \(runtime.path). Importa el runtime y recompila."
            return false
        }
        let configured: Int32
        if Bundle.main.bundleURL.pathExtension == "app" {
            configured = vlcshim_configure_paths(
                Bundle.main.bundleURL.appendingPathComponent("Contents/Frameworks/VLCLibraries").path,
                Bundle.main.bundleURL.appendingPathComponent("Contents/PlugIns/VLCPlugins").path,
                runtime.appendingPathComponent("share").path)
        } else {
            configured = vlcshim_configure_runtime(runtime.path)
        }
        guard configured == 0 else {
            errorMessage = String(cString: vlcshim_last_error())
            return false
        }
        appendLog("VLC local: \(runtime.path)")
        let rc = vlcshim_init()
        guard rc == 0 else {
            errorMessage = "No se pudo cargar libvlc (\(rc)): \(String(cString: vlcshim_last_error()))"
            return false
        }
        vlcReady = true
        attachVideoView()
        vlcshim_set_volume(Int32(volume))
        vlcshim_set_mute(muted ? 1 : 0)
        return true
    }

    private func start(_ intent: SessionLifecycle.Intent) {
        guard canStart, let cfg = config else { return }
        if intent == .play {
            loadChannels()
            guard !channels.isEmpty else {
                errorMessage = "No hay lista valida. Usa Escanear o importa una lista."
                return
            }
            guard preparePlayer() else { return }
        }
        guard let token = lifecycle.begin(intent) else { return }
        expectedStop = false
        errorMessage = nil
        scanning = intent == .scan
        pendingScanSave = false
        scanFound = 0
        scanProgress = "Preparando receptor…"
        tuning = false
        lastProgram = nil
        resetSignal()
        let rec = ReceiverProcess()
        receiver = rec
        rec.onLine = { [weak self] line in
            guard let self, self.lifecycle.generation == token else { return }
            self.handle(line, token: token)
        }
        rec.onExit = { [weak self] code in self?.receiverExited(token: token, code: code) }
        do {
            try rec.launch(
                binary: URL(fileURLWithPath: startMode == .warm ? cfg.childWarm : cfg.childCold),
                firmware: URL(fileURLWithPath: cfg.firmware),
                arguments: [intent == .scan ? "--scan" : "--play"],
                workDir: URL(fileURLWithPath: cfg.workDir),
                vlcNeutral: URL(fileURLWithPath: cfg.vlcNeutral))
            appendLog("Proceso iniciado en modo \(startMode.rawValue); esperando receptor listo.")
        } catch {
            appendLog("No se pudo iniciar: \(error.localizedDescription)")
            receiverExited(token: token, code: 1)
        }
    }

    private func stopReceiver(next: SessionLifecycle.Intent?) {
        guard hasReceiver, lifecycle.phase != .stopping else { return }
        let interrupt = scanning || lifecycle.phase == .starting
        expectedStop = true
        lifecycle.stop(next: next)
        pendingScanSave = false
        tuning = false
        stopVideo()
        appendLog("Esperando salida real del receptor antes de liberar la sesion.")
        // Do not nil out `receiver`: its exit callback is the sole release point.
        receiver?.requestStop(interrupt: interrupt)
    }

    private func receiverExited(token: UUID, code: Int32) {
        guard lifecycle.generation == token else { return }
        let wasExpected = expectedStop
        let next = lifecycle.finish(token, code: code)
        receiver = nil
        scanning = false
        pendingScanSave = false
        tuning = false
        lastProgram = nil
        stopVideo()
        currentTitle = "Sin canal"
        resetSignal()
        appendLog("Salida confirmada del proceso (codigo \(code)). USB ya no pertenece a este hijo.")
        if isQuitting {
            let completion = terminationCompletion
            terminationCompletion = nil
            completion?()
            return
        }
        if code != 0 {
            errorMessage = wasExpected
                ? "El receptor termino con error. No se iniciara otro automaticamente; revisa Diagnostico."
                : "Fallo al iniciar o recibir. No se recargara firmware automaticamente. Revisa Diagnostico."
        } else if !wasExpected {
            appendLog("El receptor termino sin una solicitud de cierre.")
        }
        if let next {
            // Nothing is scheduled until the child has actually exited.
            startMode = .warm
            start(next)
        }
    }

    /// Called by AppDelegate's terminate-later path (also for Cmd-Q).
    func quitApplication(completion: @escaping () -> Void) {
        isQuitting = true
        stopRecording()
        terminationCompletion = completion
        if hasReceiver {
            let interrupt = scanning || lifecycle.phase == .starting
            // Cancel any queued scan/reload while quitting.
            lifecycle.stop(next: nil)
            expectedStop = true
            pendingScanSave = false
            stopVideo()
            receiver?.requestStop(interrupt: interrupt)
        } else {
            stopVideo()
            tsrelay_shutdown()
            terminationCompletion = nil
            completion()
        }
    }

    // MARK: Playback and scans. No cold retry is ever inferred.

    func playVideo() {
        guard canChooseChannel, let id = selectedId,
              let ch = channels.first(where: { $0.id == id }), preparePlayer() else { return }
        let rc = vlcshim_play("udp://@127.0.0.1:1235", Int32(ch.program), 400)
        videoPlaying = rc >= 0
        if rc < 0 { errorMessage = String(cString: vlcshim_last_error()) }
    }
    func stopVideo() {
        stopRecording()
        videoPlaying = false
        if vlcReady { vlcshim_stop() }
    }

    func changeChannel(id: Int) {
        guard canChooseChannel,
              let channel = channels.first(where: { $0.id == id }), preparePlayer() else { return }
        selectedId = id
        lastProgram = Int32(channel.program)
        currentTitle = channel.title
        resetSignal()
        tuning = true
        stopVideo()
        if receiver?.send("\(id)") != true {
            tuning = false
            errorMessage = "No se pudo enviar la seleccion al receptor."
        }
    }
    func attachVideoView() {
        guard vlcReady, let view = videoNSView else { return }
        vlcshim_set_view(Unmanaged.passUnretained(view).toOpaque())
    }
    func startScan() {
        guard canRequestScan else { return }
        if hasReceiver { stopReceiver(next: .scan) }
        else { start(.scan) }
    }
    func cancelScan() {
        guard scanning, lifecycle.phase != .stopping else { return }
        stopReceiver(next: nil)
    }
    func answerScanSave(save: Bool) {
        guard scanning, lifecycle.phase != .stopping else { return }
        pendingScanSave = false
        receiver?.send(save ? "GUARDAR" : "")
    }

    private func handle(_ line: String, token: UUID) {
        appendLog(line)
        if line.contains("FIRMWARE RESPONSE VERIFIED.") || line.contains("WARM CHECK OK.") {
            lifecycle.verifyFirmware(token)
            startMode = .warm
            appendLog("Firmware verificado: siguientes aperturas en Caliente, sin recarga.")
        }
        // This is printed only AFTER loader, activation, optional scan and socket setup.
        if line.hasPrefix("m + Enter: lista") {
            guard lifecycle.markReady(token) else { return }
            if scanning { loadChannels() }
            scanning = false
            pendingScanSave = false
            startMode = .warm
            appendLog("RECEPTOR LISTO. Ya puedes seleccionar un canal.")
        } else if line.hasPrefix("SCAN [") {
            scanProgress = line
        } else if line.hasPrefix("ENCONTRADO: ") {
            scanFound += 1
        } else if line.contains("Guardar estos"), lifecycle.phase != .stopping {
            pendingScanSave = true
        } else if line.hasPrefix("Signal SNR=") {
            parseStatus(line)
        } else if line.hasPrefix("VLC child PID="), let program = lastProgram,
                  lifecycle.phase == .ready {
            // The neutral child confirms selection. libvlc receives the same SID.
            let rc = vlcshim_play("udp://@127.0.0.1:1235", program, 400)
            videoPlaying = rc >= 0
            if rc < 0 { errorMessage = String(cString: vlcshim_last_error()) }
            tuning = false
        } else if line.hasPrefix("No lock.") || line.hasPrefix("No USB data for ten timeouts") {
            tuning = false
            stopVideo()
            resetSignal()
        }
    }
    private func resetSignal() {
        snrText = "—"
        frameLock = false
        atscLock = false
        fecLock = false
    }

    // MARK: Recording the original full transport stream, independent of VLC.

    private func recorderError() -> String {
        var buffer = [CChar](repeating: 0, count: 1024)
        let capacity = buffer.count
        tsrelay_copy_error(&buffer, capacity)
        return String(cString: buffer)
    }

    func startRecording() {
        guard canRecord else { return }
        let panel = NSSavePanel()
        panel.title = "Grabar transmision en formato TS"
        panel.message = "Guarda el flujo original completo de esta frecuencia, incluidos sus subcanales. Usa un nombre nuevo; no se sobrescriben archivos."
        panel.allowedContentTypes = [UTType(filenameExtension: "ts") ?? .data]
        panel.canCreateDirectories = true
        panel.isExtensionHidden = false
        panel.directoryURL = FileManager.default.urls(for: .moviesDirectory,
                                                      in: .userDomainMask).first
        let date = DateFormatter()
        date.dateFormat = "yyyy-MM-dd_HH-mm-ss"
        let title = String(currentTitle.replacingOccurrences(
            of: "[^\\p{L}\\p{N} _.-]", with: "_", options: .regularExpression).prefix(80))
        panel.nameFieldStringValue = "\(title)_\(date.string(from: Date())).ts"
        guard panel.runModal() == .OK, let chosen = panel.url, canRecord else { return }
        let url = chosen.pathExtension.lowercased() == "ts"
            ? chosen : chosen.appendingPathExtension("ts")
        guard tsrelay_start_recording(url.path) == 0 else {
            errorMessage = recorderError()
            appendLog("No se pudo iniciar la grabacion: \(errorMessage ?? "")")
            return
        }
        recordingURL = url
        recordingBytes = 0
        recordingSeconds = 0
        recordingStart = Date()
        isRecording = true
        appendLog("REC: \(url.path). TS original de la frecuencia completa; no se recodifica.")
        recordingTimer?.invalidate()
        recordingTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in self?.refreshRecording() }
        }
    }

    private func refreshRecording() {
        guard isRecording else { return }
        recordingBytes = tsrelay_recorded_bytes()
        recordingSeconds = max(0, Int(Date().timeIntervalSince(recordingStart ?? Date())))
        if tsrelay_is_recording() == 0 {
            errorMessage = recorderError()
            appendLog("Grabacion detenida por error: \(errorMessage ?? ""). El TS puede estar incompleto.")
            stopRecording()
        }
    }

    func stopRecording() {
        guard isRecording else { return }
        recordingTimer?.invalidate()
        recordingTimer = nil
        let result = tsrelay_stop_recording()
        recordingBytes = tsrelay_recorded_bytes()
        recordingSeconds = max(0, Int(Date().timeIntervalSince(recordingStart ?? Date())))
        isRecording = false
        recordingStart = nil
        if result < 0 {
            errorMessage = recorderError()
            appendLog("STOP con error: \(errorMessage ?? ""). Conserva el archivo parcial.")
        } else {
            appendLog("STOP: TS cerrado, \(recordingBytes) bytes. \(recordingURL?.path ?? "")")
            if recordingBytes == 0 {
                errorMessage = "El TS quedo vacio: no llegaron datos durante la grabacion. Revisa la senal."
            }
        }
    }

    func revealRecording() {
        guard let url = recordingURL else { return }
        NSWorkspace.shared.activateFileViewerSelecting([url])
    }

    func shutdownRecorder() {
        stopRecording()
        tsrelay_shutdown()
    }
    private func parseStatus(_ line: String) {
        for token in line.split(separator: " ") {
            if token.hasPrefix("SNR=") { snrText = String(token.dropFirst(4)) }
            else if token.hasPrefix("frame=") { frameLock = (Int(token.dropFirst(6)) ?? 0) != 0 }
            else if token.hasPrefix("ATSC=") { atscLock = (Int(token.dropFirst(5)) ?? 0) != 0 }
            else if token.hasPrefix("FEC=") { fecLock = (Int(token.dropFirst(4)) ?? 0) != 0 }
        }
    }
    func appendLog(_ line: String) {
        logLines.append(line)
        if logLines.count > 2000 { logLines.removeFirst(logLines.count - 2000) }
        try? logFile?.write(contentsOf: Data((line + "\n").utf8))
    }
}
