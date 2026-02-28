#include "serverconnector.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <qcontainerfwd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "qdir.h"
#include "qlogging.h"

static std::mutex transact_mutex;

ServerConnector::ServerConnector() : session_socket_(-1) {}

ServerConnector::~ServerConnector() { endSession(); }

std::string ServerConnector::getSocketPath() {
    const char* xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    uid_t uid = getuid();
    std::string sockname = "hazkey-server." + std::to_string(uid) + ".sock";
    if (xdg_runtime_dir && xdg_runtime_dir[0] != '\0') {
        return std::string(xdg_runtime_dir) + "/" + sockname;
    } else {
        return "/tmp/" + sockname;
    }
}

bool writeAll(int fd, const void* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, (const char*)data + sent, len - sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(fd, &wfds);
                timeval tv = {2, 0};  // 2sec timeout
                int r = select(fd + 1, NULL, &wfds, NULL, &tv);
                if (r <= 0) {
                    return false;
                }
                continue;
            }
            return false;
        }
        sent += n;
    }
    return true;
}

bool readAll(int fd, void* data, size_t len) {
    size_t recved = 0;
    while (recved < len) {
        ssize_t n = read(fd, (char*)data + recved, len - recved);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(fd, &rfds);
                timeval tv = {10, 0};  // 2sec timeout
                int r = select(fd + 1, &rfds, NULL, NULL, &tv);
                if (r <= 0) {
                    return false;
                }
                continue;
            }
            return false;
        }
        if (n == 0) return false;  // closed
        recved += n;
    }
    return true;
}

int ServerConnector::createConnection() {
    std::string socket_path = getSocketPath();

    // try restarting server only 1 time
    // on 1st attempt (minus 1)
    constexpr int ATTEMPT_TRY_START = 0;
    // on 4th attempt (minus 1)
    constexpr int ATTEMPT_TRY_START_FORCE = 3;

    constexpr int MAX_RETRIES = 8;
    constexpr int RETRY_INTERVAL_MS = 250;

    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(RETRY_INTERVAL_MS));
            continue;
        }

        int fcntlRes =
            fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
        if (fcntlRes != 0) {
            close(sock);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(RETRY_INTERVAL_MS));
            continue;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        int ret = connect(sock, (sockaddr*)&addr, sizeof(addr));
        if (ret == 0) {
            // Connected
            return sock;
        }
        if (errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            timeval tv = {2, 0};
            int sel = select(sock + 1, NULL, &wfds, NULL, &tv);
            if (sel > 0 && FD_ISSET(sock, &wfds)) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    // Connected
                    return sock;
                }
            }
        }
        close(sock);
        if (attempt == ATTEMPT_TRY_START) {
            QProcess::startDetached("hazkey-server", {}, "/");
        } else if (attempt == ATTEMPT_TRY_START_FORCE) {
            QProcess::startDetached("hazkey-server", {"-r"}, "/");
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(RETRY_INTERVAL_MS));
    }
    return -1;
}

std::optional<hazkey::ResponseEnvelope> ServerConnector::transactOnSocket(
    int sock, const hazkey::RequestEnvelope& send_data) {
    std::string msg;
    if (!send_data.SerializeToString(&msg)) {
        return std::nullopt;
    }

    // write length
    uint32_t writeLen = htonl(msg.size());
    if (!writeAll(sock, &writeLen, 4)) {
        return std::nullopt;
    }

    // write data
    if (!writeAll(sock, msg.c_str(), msg.size())) {
        return std::nullopt;
    }

    // read response length
    uint32_t readLenBuf;
    if (!readAll(sock, &readLenBuf, 4)) {
        return std::nullopt;
    }

    uint32_t readLen = ntohl(readLenBuf);

    if (readLen > 2 * 1024 * 1024) {  // 2MB limit
        return std::nullopt;
    }

    // read response
    std::vector<char> buf(readLen);
    if (!readAll(sock, buf.data(), readLen)) {
        return std::nullopt;
    }

    hazkey::ResponseEnvelope resp;
    if (!resp.ParseFromArray(buf.data(), readLen)) {
        return std::nullopt;
    }

    return resp;
}

std::optional<hazkey::ResponseEnvelope> ServerConnector::transact(
    const hazkey::RequestEnvelope& send_data) {
    std::lock_guard<std::mutex> lock(transact_mutex);

    // Create new connection for each transaction
    int sock = createConnection();
    if (sock == -1) {
        return std::nullopt;
    }

    auto resp = transactOnSocket(sock, send_data);

    // Close connection after transaction
    close(sock);
    return resp;
}

bool ServerConnector::beginSession() {
    std::lock_guard<std::mutex> lock(transact_mutex);

    // Close existing session if any
    if (session_socket_ != -1) {
        close(session_socket_);
        session_socket_ = -1;
    }

    session_socket_ = createConnection();
    return session_socket_ != -1;
}

void ServerConnector::endSession() {
    std::lock_guard<std::mutex> lock(transact_mutex);

    if (session_socket_ != -1) {
        close(session_socket_);
        session_socket_ = -1;
    }
}

std::optional<hazkey::config::CurrentConfig>
ServerConnector::getConfigInSession() {
    std::lock_guard<std::mutex> lock(transact_mutex);

    if (session_socket_ == -1) {
        return std::nullopt;
    }

    hazkey::RequestEnvelope request;
    auto _ = request.mutable_get_config();
    auto response = transactOnSocket(session_socket_, request);
    if (response == std::nullopt) {
        return std::nullopt;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        return std::nullopt;
    }
    if (!responseVal.has_current_config()) {
        return std::nullopt;
    }
    return responseVal.current_config();
}

bool ServerConnector::reloadZenzaiModelInSession() {
    std::lock_guard<std::mutex> lock(transact_mutex);

    if (session_socket_ == -1) {
        return false;
    }

    hazkey::RequestEnvelope request;
    auto _ = request.mutable_reload_zenzai_model();
    auto response = transactOnSocket(session_socket_, request);
    if (response == std::nullopt) {
        return false;
    }
    auto responseVal = response.value();
    return responseVal.status() == hazkey::SUCCESS;
}

std::optional<hazkey::config::CurrentConfig> ServerConnector::getConfig() {
    hazkey::RequestEnvelope request;
    auto _ = request.mutable_get_config();
    auto response = transact(request);
    if (response == std::nullopt) {
        return std::nullopt;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        return std::nullopt;
    }
    if (!responseVal.has_current_config()) {
        return std::nullopt;
    }
    return responseVal.current_config();
}

void ServerConnector::setCurrentConfig(
    hazkey::config::CurrentConfig currentConfig) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_set_config();
    *props->mutable_profiles() = currentConfig.profiles();
    auto response = transact(request);
    if (response == std::nullopt) {
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        return;
    }
}

bool ServerConnector::clearAllHistory(const std::string& profileId) {
    hazkey::RequestEnvelope request;
    auto clearRequest = request.mutable_clear_all_history();
    clearRequest->set_profile_id(profileId);
    auto response = transact(request);
    if (response == std::nullopt) {
        return false;
    }
    auto responseVal = response.value();
    return responseVal.status() == hazkey::SUCCESS;
}

bool ServerConnector::reloadZenzaiModel() {
    hazkey::RequestEnvelope request;
    auto _ = request.mutable_reload_zenzai_model();
    auto response = transact(request);
    if (response == std::nullopt) {
        return false;
    }
    auto responseVal = response.value();
    return responseVal.status() == hazkey::SUCCESS;
}
