import Foundation

#if canImport(Glibc)
import Glibc
#elseif canImport(Musl)
import Musl
#else
#error("Unsupported platform")
#endif

import Glibc

enum ProcessManagerError: Error {
    case lockCreationFailed
    case anotherInstanceRunning
    case terminationFailed
}

class ProcessManager {
    private let uid: uid_t
    private let pid: pid_t
    private let lockFilePath: String
    private var lockFd: Int32 = -1

    init(lockFilePath: String) {
        self.uid = getuid()
        self.pid = getpid()
        self.lockFilePath = lockFilePath
    }

    deinit {
        if lockFd != -1 {
            close(lockFd)
        }
    }

    func tryLock(force: Bool) throws {
        // parent directory is created by HazkeyServer.start()

        // try lock
        self.lockFd = open(lockFilePath, O_CREAT | O_RDWR, 0o600)
        guard self.lockFd != -1 else {
            NSLog("Failed to get lock info.")
            throw ProcessManagerError.lockCreationFailed
        }

        if flock(lockFd, LOCK_EX | LOCK_NB) != 0 {
            // lock fail
            if let (oldPid, versionMatch) = readLockFile() {
                if !force, versionMatch {
                    NSLog("Another hazkey-server is already running.")
                    NSLog("Use -r or --replace option to replace the existing server.")
                    throw ProcessManagerError.anotherInstanceRunning
                }

                if !versionMatch {
                    NSLog("Version mismatch detected. Terminating old server...")
                }

                // terminate process
                if kill(oldPid, 0) == 0 {
                    try terminateAnotherServer(pid: oldPid)
                }
            } else {
                // broken lockfile
                NSLog("Failed to read existing lock info.")
                terminateOtherServers()
            }

            // retry lock
            close(lockFd)
            self.lockFd = open(lockFilePath, O_CREAT | O_RDWR, 0o600)
            if flock(self.lockFd, LOCK_EX | LOCK_NB) != 0 {
                NSLog("Failed to acquire lock after terminating existing process.")
                throw ProcessManagerError.anotherInstanceRunning
            }
        }

        // write current process info
        writeLockFile()
    }

    private func readLockFile() -> (Int32, Bool)? {
        let capacity = 256
        lseek(lockFd, 0, SEEK_SET)
        let buffer = UnsafeMutablePointer<Int8>.allocate(capacity: capacity)
        buffer.initialize(repeating: 0, count: capacity)
        defer { buffer.deallocate() }
        // capacity - 1 because last byte should be 0
        let bytesRead = read(lockFd, buffer, capacity - 1)
        guard bytesRead > 0 else { return nil }
        let fullContent = String(cString: buffer)
        let lines = fullContent.components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespaces) }
        guard lines.count >= 2 else { return nil }
        let versionMatch = lines[1] == hazkeyVersion
        guard let pid = Int32(lines[0]) else { return nil }
        return (pid, versionMatch)
    }

    private func writeLockFile() {
        guard ftruncate(lockFd, 0) == 0 else {
            NSLog("Failed to truncate lock file")
            return
        }
        lseek(lockFd, 0, SEEK_SET)
        let info = "\(getpid())\n\(hazkeyVersion)\n"
        let written = write(lockFd, info, info.utf8.count)
        if written != info.utf8.count {
            NSLog("Failed to write complete lock file data")
        }
        fsync(lockFd)
    }

    private func getOtherServerPIDs() throws -> [Int32] {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        task.arguments = ["pgrep", "-u", String(uid), "-x", "hazkey-server"]
        let pipe = Pipe()
        task.standardOutput = pipe

        try task.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        task.waitUntilExit()

        guard let output = String(data: data, encoding: .utf8) else { return [] }

        let pidString = String(pid)

        return output.components(separatedBy: .newlines)
            .compactMap { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty && $0 != pidString }
            .compactMap { Int32($0) }
    }

    private func terminateAnotherServer(pid: pid_t) throws {
        NSLog("Terminating existing server with PID \(pid)...")

        // Send SIGTERM to gracefully terminate
        if kill(pid, SIGTERM) != 0 { return }

        for attempt in 1...30 {  // 30 try * 0.1 sec
            usleep(100_000)  // 0.1 sec

            // Check if process is still running
            if kill(pid, 0) != 0 {
                NSLog("Existing server terminated successfully")
                return
            }

            if attempt == 15 {  // try SIGKILL
                NSLog("Server didn't respond to SIGTERM, sending SIGKILL...")
                kill(pid, SIGKILL)
            }
        }

        // Final check
        if kill(pid, 0) == 0 {
            NSLog("Failed to terminate existing server")
            throw ProcessManagerError.terminationFailed
        }

        NSLog("Existing server terminated")
    }

    private func terminateOtherServers() {
        let otherPids: [Int32]
        // terminate processes running without lock
        do {
            otherPids = try getOtherServerPIDs()
        } catch {
            // continue even if getOtherServerPIDs() fails.
            // generally no problem because servers are already terminated.
            NSLog("getOtherServerPIDs failed: \(error)")
            otherPids = []
        }
        for killPid in otherPids {
            try? terminateAnotherServer(pid: killPid)
        }
    }
}
