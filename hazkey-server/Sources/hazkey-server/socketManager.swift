import Foundation

protocol SocketManagerDelegate: AnyObject {
    func socketManager(_ manager: SocketManager, didReceiveData data: Data, from clientFd: Int32)
        -> Data
    func socketManager(_ manager: SocketManager, clientDidConnect clientFd: Int32)
    func socketManager(_ manager: SocketManager, clientDidDisconnect clientFd: Int32)
}

class SocketManager {
    weak var delegate: SocketManagerDelegate?

    private var signalSources: [DispatchSourceSignal] = []
    private var continueServing = true

    private var serverFd: Int32 = -1
    private var currentClientFd: Int32?
    private let socketPath: String
    private var pipeFds: [Int32] = [-1, -1]

    private func stopServing(reason: String) {
        guard continueServing else { return }
        NSLog(reason)
        continueServing = false
        if pipeFds[1] != -1 {
            close(pipeFds[1])
            pipeFds[1] = -1
        }
    }

    init(socketPath: String) {
        self.socketPath = socketPath
    }

    deinit {
        closeSocket()
    }

    func setupSocket() throws {
        unlink(socketPath)

        serverFd = socket(AF_UNIX, Int32(SOCK_STREAM.rawValue), 0)
        guard serverFd != -1 else {
            throw SocketError.readFailed("Failed to create socket", errno)
        }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        strncpy(&addr.sun_path.0, socketPath, MemoryLayout.size(ofValue: addr.sun_path))

        let addrSize = socklen_t(MemoryLayout.size(ofValue: addr))
        let bindResult = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(serverFd, $0, addrSize)
            }
        }

        guard bindResult != -1 else {
            throw SocketError.readFailed("Failed to bind socket", errno)
        }

        guard chmod(socketPath, 0o600) != -1 else {
            throw SocketError.readFailed("Failed to set socket permissions", errno)
        }

        guard listen(serverFd, 10) != -1 else {
            throw SocketError.readFailed("Failed to listen", errno)
        }

        // Set non-blocking
        let flags = fcntl(serverFd, F_GETFL, 0)
        let fcntlRes = fcntl(serverFd, F_SETFL, flags | O_NONBLOCK)
        if fcntlRes != 0 {
            NSLog("fcntl() failed")
        }

        var fds: [Int32] = [0, 0]
        guard pipe(&fds) != -1 else {
            throw SocketError.readFailed("Failed to bind pipe socket", errno)
        }
        pipeFds = fds
    }

    private func setupSignalHandlers() {
        signal(SIGPIPE, SIG_IGN)

        let signalQueue = DispatchQueue(label: "dev.hiira.hazkey.server.socketmanager.signals")
        let signals = [SIGINT, SIGTERM, SIGHUP]

        for sig in signals {
            signal(sig, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: sig, queue: signalQueue)
            source.setEventHandler { [weak self] in
                self?.stopServing(reason: "Signal \(sig) received, shutting down...")
            }
            source.resume()
            self.signalSources.append(source)
        }
    }

    func startListening() {
        setupSignalHandlers()
        while continueServing {
            var pollFds: [pollfd] = []

            // Always poll the server socket for new connections
            pollFds.append(pollfd(fd: serverFd, events: Int16(POLLIN), revents: 0))

            // poll stopper
            pollFds.append(pollfd(fd: pipeFds[0], events: Int16(POLLIN), revents: 0))

            // If we have a current client, also poll it
            if let clientFd = currentClientFd {
                pollFds.append(pollfd(fd: clientFd, events: Int16(POLLIN), revents: 0))
            }

            let pollRes = poll(&pollFds, nfds_t(pollFds.count), 1000)

            if pollRes < 0 {
                if errno == EINTR {
                    // signal received
                    if !continueServing {
                        break
                    }
                    continue
                }
                NSLog("Poll failed: \(errno)")
                break
            }

            if pollRes == 0 {
                // Timeout
                continue
            }

            // pipe closed by signalhandler
            if pollFds[1].revents & Int16(POLLIN|POLLHUP) != 0 {
                break
            }

            // Check if server socket has a new connection
            if pollFds[0].revents & Int16(POLLIN) != 0 {
                handleNewConnection()
            }

            // Check if current client has data
            if pollFds.count > 2, let clientFd = currentClientFd {
                let clientEvents = Int32(pollFds[2].revents)

                if clientEvents & POLLHUP != 0 || clientEvents & POLLERR != 0 {
                    NSLog("Client disconnected or error: \(clientFd)")
                    closeClient(clientFd)
                    continue
                }

                if clientEvents & POLLIN != 0 {
                    handleClientData(clientFd)
                }
            }
        }
    }

    private func handleNewConnection() {
        var clientAddr = sockaddr()
        var clientLen: socklen_t = socklen_t(MemoryLayout<sockaddr>.size)
        let newClientFd = accept(serverFd, &clientAddr, &clientLen)

        if newClientFd != -1 {
            // If we already have a client, close it
            if let existingClientFd = currentClientFd {
                NSLog("New client connecting, closing existing client: \(existingClientFd)")
                closeClient(existingClientFd)
            }

            // Set up the new client
            NSLog("Client connected: \(newClientFd)")

            // Make client non-blocking
            let clientFlags = fcntl(newClientFd, F_GETFL, 0)
            let fcntlRes = fcntl(newClientFd, F_SETFL, clientFlags | O_NONBLOCK)
            if fcntlRes != 0 {
                NSLog("fcntl() failed for client")
                close(newClientFd)
                currentClientFd = nil
            } else {
                currentClientFd = newClientFd
                delegate?.socketManager(self, clientDidConnect: newClientFd)
            }
        }
    }

    private func handleClientData(_ clientFd: Int32) {
        do {
            // Handle client request
            let maxMessageSize: UInt32 = 1024 * 1024  // 1MB limit

            // Read message length header
            debugLog("Reading data from client \(clientFd)...")
            let lengthData = try readData(from: clientFd, count: 4)
            let readLen = lengthData.withUnsafeBytes {
                $0.load(as: UInt32.self).bigEndian
            }
            debugLog("Message length: \(readLen)")

            // Sanity check
            guard readLen <= maxMessageSize else {
                throw SocketError.messageTooLarge(readLen)
            }

            // Read message body
            let query = try readData(from: clientFd, count: Int(readLen))
            debugLog("Successfully read \(query.count) bytes")

            // Process and respond
            let response =
                delegate?.socketManager(self, didReceiveData: query, from: clientFd) ?? Data()
            debugLog("Processed request, response size: \(response.count)")

            // Write response length
            var writeLen = UInt32(response.count).bigEndian
            let lengthHeader = withUnsafeBytes(of: &writeLen) { Data($0) }
            try writeData(to: clientFd, data: lengthHeader)

            // Write response body
            try writeData(to: clientFd, data: response)

            fsync(clientFd)
            debugLog("Successfully wrote response")

        } catch let error as SocketError {
            handleSocketError(error, clientFd: clientFd)
        } catch {
            NSLog("An unexpected error occurred: \(error)")
            closeClient(clientFd)
        }
    }

    private func handleSocketError(_ error: SocketError, clientFd: Int32) {
        switch error {
        case .clientDisconnected(let msg):
            NSLog(msg)
        case .readFailed(let msg, let err):
            NSLog("Read failed: \(msg), errno: \(err)")
        case .incompleteRead(let msg), .incompleteWrite(let msg):
            NSLog(msg)
        case .messageTooLarge(let len):
            NSLog("Message too large: \(len)")
        case .writeFailed(let msg, let err):
            NSLog("Write failed: \(msg), errno: \(err)")
        default:
            NSLog("Socket error: \(error)")
        }
        closeClient(clientFd)
    }

    private func closeClient(_ clientFd: Int32) {
        NSLog("Closing client connection: \(clientFd)")
        close(clientFd)
        if currentClientFd == clientFd {
            currentClientFd = nil
        }
        delegate?.socketManager(self, clientDidDisconnect: clientFd)
    }

    func closeSocket() {
        if let clientFd = currentClientFd {
            close(clientFd)
            currentClientFd = nil
        }

        if serverFd != -1 {
            close(serverFd)
            serverFd = -1
        }

        unlink(socketPath)
    }
}
