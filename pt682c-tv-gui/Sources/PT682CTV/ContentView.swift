import SwiftUI

@MainActor
struct ContentView: View {
    @ObservedObject var state: AppState

    var body: some View {
        VStack(spacing: 0) {
            topBar
            Divider()
            NavigationSplitView {
                sidebar
            } detail: {
                videoArea
            }
            Divider()
            recordingBar
            Divider()
            bottomBar
            if let error = state.errorMessage {
                errorStrip(error)
            }
        }
        .frame(minWidth: 1000, minHeight: 680)
        .sheet(isPresented: $state.showLog) {
            logSheet
        }
        .confirmationDialog(
            "Guardar los programas encontrados",
            isPresented: $state.pendingScanSave,
            titleVisibility: .visible
        ) {
            Button("Guardar lista nueva") { state.answerScanSave(save: true) }
            Button("Conservar lista anterior") { state.answerScanSave(save: false) }
            Button("Cancelar", role: .cancel) { state.answerScanSave(save: false) }
        } message: {
            Text("La lista nueva reemplaza a la guardada (se crea una copia de respaldo).")
        }
    }

    // MARK: Barra superior

    private var topBar: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(state.connected ? Color.green : Color.red)
                .frame(width: 10, height: 10)
            Text(state.statusText)

            Picker("Inicio", selection: $state.startMode) {
                Text("Caliente").tag(StartMode.warm)
                Text("Frio").tag(StartMode.cold)
            }
            .pickerStyle(.segmented)
            .frame(width: 150)
            .disabled(state.hasReceiver)
            .help("Caliente: reapertura sin reconectar USB. Frio: tras conectar el PT682C.")

            if state.scanning {
                ProgressView()
                    .controlSize(.small)
                Text(state.scanProgress)
                    .lineLimit(1)
                Text("(\(state.scanFound) encontrados)")
                    .foregroundStyle(.secondary)
                Button("Cancelar escaneo") { state.cancelScan() }
                    .disabled(state.lifecycle.phase == .stopping)
            } else {
                Button(state.connected ? "Desconectar" : "Conectar") {
                    state.connect()
                }
                .disabled(!state.canStart && !state.connected)
            }

            Spacer()

            Text(state.currentTitle)
                .lineLimit(1)
                .frame(maxWidth: 280)

            Spacer()

            Text("SNR \(state.snrText)")
                .monospacedDigit()

            statusDot(on: state.frameLock, label: "F")
            statusDot(on: state.atscLock, label: "A")
            statusDot(on: state.fecLock, label: "C")
        }
        .padding(8)
    }

    private func statusDot(on: Bool, label: String) -> some View {
        HStack(spacing: 2) {
            Circle().fill(on ? Color.green : Color.gray).frame(width: 7, height: 7)
            Text(label).font(.caption)
        }
        .help("Ultima lectura durante sintonizacion: F frame, A ATSC, C FEC. No es monitor continuo.")
    }

    // MARK: Lista de canales

    private var sidebar: some View {
        List(selection: $state.selectedId) {
            ForEach(state.channels) { channel in
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(channel.title).lineLimit(1)
                        Text(String(format: "%.1f MHz · programa %d",
                                    channel.frequencyMHz, channel.program))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                }
                .tag(channel.id)
                .padding(.vertical, 2)
            }
        }
        .listStyle(.sidebar)
        .frame(minWidth: 230)
        .disabled(!state.canChooseChannel)
        .help(state.isRecording ? "Deten la grabacion con STOP antes de cambiar de canal." : "Seleccionar canal")
        .onChange(of: state.selectedId) { newValue in
            if let id = newValue, !state.scanning {
                state.changeChannel(id: id)
            }
        }
        .overlay {
            if state.channels.isEmpty {
                Text("Sin canales. Usa Escanear para generar una lista.")
                    .foregroundStyle(.secondary)
                    .padding()
            }
        }
    }

    // MARK: Video

    private var videoArea: some View {
        ZStack {
            VideoContainerView(state: state)
            if state.scanning {
                Text("Escaneando frecuencias. La ventana de video se reanuda al terminar.")
                    .foregroundStyle(.white.opacity(0.6))
            } else if !state.connected {
                Text("Conecta el receptor para ver television")
                    .foregroundStyle(.white.opacity(0.55))
            }
        }
        .background(Color.black)
    }

    // MARK: Barra inferior

    private var recordingBar: some View {
        HStack(spacing: 12) {
            Button {
                state.startRecording()
            } label: {
                Label("REC", systemImage: "record.circle")
                    .foregroundStyle(.red)
            }
            .disabled(!state.canRecord)
            .help("Guardar el flujo TS original completo de la frecuencia, sin reiniciar la TV.")

            Button {
                state.stopRecording()
            } label: {
                Label("STOP", systemImage: "stop.fill")
            }
            .disabled(!state.isRecording)
            .help("Finalizar solamente la grabacion. La television sigue reproduciendose.")

            if state.recordingURL != nil {
                Circle()
                    .fill(state.isRecording ? Color.red : Color.gray)
                    .frame(width: 8, height: 8)
                Text(state.isRecording ? "Grabando" : "Grabacion finalizada")
                Text(state.recordingSummary).monospacedDigit()
                Text(state.recordingURL?.lastPathComponent ?? "")
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .help(state.recordingURL?.path ?? "")
                Spacer(minLength: 0)
                Button("Mostrar en Finder") { state.revealRecording() }
                    .disabled(state.isRecording)
            } else {
                Text("TS original · frecuencia completa · sin recodificar")
                    .foregroundStyle(.secondary)
                Spacer()
            }
        }
        .font(.callout)
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
    }

    private var bottomBar: some View {
        HStack(spacing: 10) {
            Button("Detener TV") { state.stopVideo() }
                .disabled(!state.connected || state.scanning)
            Button("Reproducir") { state.playVideo() }
                .disabled(!state.canChooseChannel || state.selectedId == nil)

            Divider().frame(height: 16)

            Slider(value: $state.volume, in: 0...100)
                .frame(width: 120)
                .help("Volumen")

            Button(state.muted ? "Silenciado" : "Silencio") {
                state.muted.toggle()
            }

            Spacer()

            Button("Escanear") { state.startScan() }
                .disabled(!state.canRequestScan)
                .help("Escanea RF 2-51 y ofrece guardar la lista nueva. No la reemplaza sin tu confirmacion.")
            Button("Recargar lista") { state.reloadChannels() }
                .disabled(state.scanning || state.tuning || state.isRecording ||
                          state.lifecycle.phase == .starting || state.lifecycle.phase == .stopping)
            Button("Importar lista...") { state.importChannels() }
                .disabled(!state.canStart)
            Button("Diagnostico") { state.showLog = true }
        }
        .padding(8)
    }

    private func errorStrip(_ message: String) -> some View {
        HStack(spacing: 8) {
            Text(message)
                .foregroundStyle(.red)
                .lineLimit(2)
            Spacer()
            Button("Cerrar") {
                state.errorMessage = nil
            }
        }
        .padding(.horizontal, 8)
        .padding(.bottom, 6)
    }

    // MARK: Diagnostico

    private var logSheet: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Diagnostico del receptor").font(.headline)
                Spacer()
                Button("Cerrar") { state.showLog = false }
            }
            .padding()
            Divider()
            ScrollView {
                Text(state.logLines.suffix(400).joined(separator: "\n"))
                    .font(.system(size: 11, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(8)
                    .textSelection(.enabled)
            }
        }
        .frame(width: 760, height: 440)
    }
}
