import Foundation
import CryptoKit

/// Immutable code/firmware live inside the app; user data never changes its seal.
enum AppStorage {
    static func prepare(bundle: URL, support: URL) throws -> AppConfig {
        let fm = FileManager.default
        let resources = bundle.appendingPathComponent("Contents/Resources")
        let binaries = bundle.appendingPathComponent("Contents/MacOS")
        let firmware = resources.appendingPathComponent("Firmware/dvb-demod-mxl692.fw")
        let expected = "8f77d5cb0de9111ed63c113cb2fd54a63e07a30040ae0b562cb025ab9c4e600d"
        let digest = SHA256.hash(data: try Data(contentsOf: firmware))
            .map { String(format: "%02x", $0) }.joined()
        guard digest == expected else {
            throw error("El firmware incluido no coincide con la version verificada.")
        }
        for name in ["pt682c_tv_cold", "pt682c_tv_warm", "vlc-neutral"] {
            guard fm.isExecutableFile(atPath: binaries.appendingPathComponent(name).path) else {
                throw error("Falta el ejecutable incluido: \(name). Recompila la app.")
            }
        }
        let data = support.appendingPathComponent("data")
        for directory in [support, data, support.appendingPathComponent("logs")] {
            try fm.createDirectory(at: directory, withIntermediateDirectories: true,
                                   attributes: [.posixPermissions: 0o700])
        }
        let channels = data.appendingPathComponent("last-scan.tsv")
        if !fm.fileExists(atPath: channels.path) {
            // Seed only once. Updates must never overwrite the user's saved scan.
            let seed = resources.appendingPathComponent("Defaults/last-scan.tsv")
            do {
                try fm.copyItem(at: seed, to: channels)
            } catch {
                // A second instance may have created it between exists and copy.
                guard fm.fileExists(atPath: channels.path) else { throw error }
            }
        }
        return AppConfig(
            childCold: binaries.appendingPathComponent("pt682c_tv_cold").path,
            childWarm: binaries.appendingPathComponent("pt682c_tv_warm").path,
            firmware: firmware.path, channelsFile: channels.path, workDir: support.path,
            vlcNeutral: binaries.appendingPathComponent("vlc-neutral").path,
            vlcRuntime: resources.appendingPathComponent("VLCRuntime").path)
    }

    private static func error(_ message: String) -> NSError {
        NSError(domain: "PT682CTV.Storage", code: 1,
                userInfo: [NSLocalizedDescriptionKey: message])
    }
}
