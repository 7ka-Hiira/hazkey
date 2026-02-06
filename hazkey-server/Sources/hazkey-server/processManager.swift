import Foundation

class ProcessManager {
    private let runtimeDir: URL
    private let uid: uid_t
    private let pidFilePath: String
    private let infoFilePath: String
    private var replaceExisting: Bool = false

    init() {
        self.uid = getuid()
        self.runtimeDir = URL(
            fileURLWithPath:
                ProcessInfo.processInfo.environment["XDG_RUNTIME_DIR"]
                ?? "/tmp/hazkey-runtime-\(uid)", isDirectory: true)
        self.pidFilePath = "\(runtimeDir.path)/hazkey-server.\(uid).pid"
        self.infoFilePath = "\(runtimeDir.path)/hazkey-server.\(uid).info"
    }

    func parseCommandLineArguments() {
        let arguments = CommandLine.arguments
        for arg in arguments {
            if arg == "-r" || arg == "--replace" {
                replaceExisting = true
                break
            }
        }
    }

    // return true when different version found
    func checkVersionMismatch() -> Bool {
        if FileManager.default.fileExists(atPath: infoFilePath) {
            if let versionString = try? String(contentsOfFile: infoFilePath, encoding: .utf8) {
                return versionString != hazkeyVersion
            }
        }
        // restart if file does not exists
        return true
    }

    func checkExistingServer() throws {
        if FileManager.default.fileExists(atPath: pidFilePath) {
            if let pidString = try? String(contentsOfFile: pidFilePath, encoding: .utf8),
                let pid = pid_t(pidString)
            {
                if kill(pid, 0) == 0 {
                    if !replaceExisting && !checkVersionMismatch() {
                        NSLog("Another hazkey-server is already running.")
                        NSLog("Use -r or --replace option to replace the existing server.")
                        exit(0)
                    }
                    if !terminateExistingServer(pid: pid) {
                        NSLog("Failed to terminate existing server. Exiting.")
                        exit(1)
                    }
                }
            }
        }
    }

    func createPidFile() throws {
        if !FileManager.default.fileExists(atPath: runtimeDir.path) {
            try FileManager.default.createDirectory(
                at: runtimeDir, withIntermediateDirectories: true,
                attributes: [FileAttributeKey.posixPermissions: 0o744])
        }
        try "\(getpid())".write(toFile: pidFilePath, atomically: true, encoding: .utf8)
    }

    func removePidFile() {
        try? FileManager.default.removeItem(atPath: pidFilePath)
    }

    func createInfoFile() throws {
        if !FileManager.default.fileExists(atPath: runtimeDir.path) {
            try FileManager.default.createDirectory(
                at: runtimeDir, withIntermediateDirectories: true,
                attributes: [FileAttributeKey.posixPermissions: 0o744])
        }
        try hazkeyVersion.write(toFile: infoFilePath, atomically: true, encoding: .utf8)
    }

    func removeInfoFile() {
        try? FileManager.default.removeItem(atPath: infoFilePath)
    }

    private func terminateExistingServer(pid: pid_t) -> Bool {
        NSLog("Terminating existing server with PID \(pid)...")

        // Send SIGTERM to gracefully terminate
        if kill(pid, SIGTERM) != 0 {
            NSLog("Failed to send SIGTERM to existing server")
            return false
        }

        for attempt in 1...40 {  // 40 try * 0.1 sec
            usleep(100_000)  // 0.1 sec

            // Check if process is still running
            if kill(pid, 0) != 0 {
                NSLog("Existing server terminated successfully")
                return true
            }

            if attempt == 20 {  // try SIGKILL
                NSLog("Server didn't respond to SIGTERM, sending SIGKILL...")
                kill(pid, SIGKILL)
            }
        }

        // Final check
        if kill(pid, 0) == 0 {
            NSLog("Failed to terminate existing server")
            return false
        }

        NSLog("Existing server terminated")
        return true
    }
}
