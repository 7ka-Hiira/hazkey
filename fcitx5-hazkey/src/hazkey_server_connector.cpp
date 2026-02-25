#include "hazkey_server_connector.h"

#include <arpa/inet.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "base.pb.h"
#include "commands.pb.h"

static std::mutex transact_mutex;

namespace {

bool writeAll(int fd, const void* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, static_cast<const char*>(data) + sent, len - sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(fd, &wfds);
                timeval tv = {2, 0};
                int r = select(fd + 1, NULL, &wfds, NULL, &tv);
                if (r <= 0) {
                    FCITX_ERROR() << "write timeout";
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
        ssize_t n = read(fd, static_cast<char*>(data) + recved, len - recved);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(fd, &rfds);
                timeval tv = {10, 0};
                int r = select(fd + 1, &rfds, NULL, NULL, &tv);
                if (r <= 0) {
                    FCITX_ERROR() << "read timeout";
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

}  // namespace

std::string HazkeyServerConnector::getSocketPath() {
    const char* xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    uid_t uid = getuid();
    std::string sockname = "hazkey-server." + std::to_string(uid) + ".sock";
    if (xdg_runtime_dir && xdg_runtime_dir[0] != '\0') {
        return std::string(xdg_runtime_dir) + "/" + sockname;
    } else {
        return "/tmp/" + sockname;
    }
}

void HazkeyServerConnector::startHazkeyServer(bool force_restart) {
    std::vector<std::string> args;
    args.reserve(2);
    args.push_back("hazkey-server");
    if (force_restart) {
        args.push_back("-r");
    }
    fcitx::startProcess(args, "/");
}

void HazkeyServerConnector::connectServer() {
    std::string socket_path = getSocketPath();

    constexpr int ATTEMPT_TRY_START = 0;
    constexpr int ATTEMPT_TRY_START_FORCE = 10;
    constexpr int MAX_RETRIES = 15;
    constexpr int RETRY_INTERVAL_MS = 300;

    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        sock_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_ < 0) {
            FCITX_ERROR() << "Failed to create socket";
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL_MS));
            continue;
        }
        int fcntlRes = fcntl(sock_, F_SETFL, fcntl(sock_, F_GETFL, 0) | O_NONBLOCK);
        if (fcntlRes != 0) {
            FCITX_ERROR() << "fcntl() failed";
            close(sock_);
            sock_ = -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL_MS));
            continue;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        int ret = connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (ret == 0) {
            return;
        }
        if (errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock_, &wfds);
            timeval tv = {2, 0};
            int sel = select(sock_ + 1, NULL, &wfds, NULL, &tv);
            if (sel > 0 && FD_ISSET(sock_, &wfds)) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock_, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    return;
                }
            }
        }
        FCITX_INFO() << "Failed to connect hazkey-server, retry " << (attempt + 1);
        close(sock_);
        sock_ = -1;
        if (attempt == ATTEMPT_TRY_START) {
            startHazkeyServer(false);
        } else if (attempt == ATTEMPT_TRY_START_FORCE) {
            startHazkeyServer(true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL_MS));
    }
    FCITX_INFO() << "Failed to connect hazkey-server after " << MAX_RETRIES << " attempts";
}

std::optional<hazkey::ResponseEnvelope> HazkeyServerConnector::transact(
    const hazkey::RequestEnvelope& send_data) {
    std::lock_guard<std::mutex> lock(transact_mutex);

    if (sock_ == -1) {
        FCITX_INFO() << "Socket not connected, attempting to connect...";
        connectServer();
        if (sock_ == -1) {
            FCITX_ERROR() << "Failed to establish connection to hazkey-server";
            return std::nullopt;
        }
    }

    std::string msg;
    if (!send_data.SerializeToString(&msg)) {
        FCITX_ERROR() << "Failed to serialize protobuf message.";
        return std::nullopt;
    }

    uint32_t writeLen = htonl(msg.size());
    if (!writeAll(sock_, &writeLen, 4)) {
        FCITX_INFO() << "Failed to communicate with server while writing data length. reconnecting...";
        close(sock_);
        sock_ = -1;
        connectServer();
        return std::nullopt;
    }

    if (!writeAll(sock_, msg.c_str(), msg.size())) {
        FCITX_INFO() << "Failed to communicate with server while writing data. reconnecting...";
        close(sock_);
        sock_ = -1;
        connectServer();
        return std::nullopt;
    }

    uint32_t readLenBuf;
    if (!readAll(sock_, &readLenBuf, 4)) {
        FCITX_ERROR() << "Failed to read buffer length.";
        close(sock_);
        sock_ = -1;
        return std::nullopt;
    }

    uint32_t readLen = ntohl(readLenBuf);
    if (readLen > 2 * 1024 * 1024) {
        FCITX_ERROR() << "Response size too large: " << readLen;
        close(sock_);
        sock_ = -1;
        return std::nullopt;
    }

    std::vector<char> buf(readLen);
    if (!readAll(sock_, buf.data(), readLen)) {
        FCITX_ERROR() << "Failed to read response body.";
        close(sock_);
        sock_ = -1;
        return std::nullopt;
    }

    hazkey::ResponseEnvelope resp;
    if (!resp.ParseFromArray(buf.data(), readLen)) {
        FCITX_ERROR() << "Failed to parse received data";
        return std::nullopt;
    }

    return resp;
}

void HazkeyServerConnector::saveLearningData() {
    hazkey::RequestEnvelope request;
    request.mutable_save_learning_data();
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting saveLearningData().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "saveLearningData: Server returned an error: "
                      << responseVal.error_message();
        return;
    }
}

void HazkeyServerConnector::resetServerState(bool completedPreedit) {
    hazkey::RequestEnvelope request;
    request.mutable_reset_state()->set_completed_preedit(completedPreedit);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting resetState().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "resetState: Server returned an error: "
                      << responseVal.error_message();
        return;
    }
}

std::optional<hazkey::commands::ClientState>
HazkeyServerConnector::processKeyEvent(
    const hazkey::commands::KeyEvent &event) {
    hazkey::RequestEnvelope request;
    *request.mutable_key_event() = event;

    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting keyEvent().";
        return std::nullopt;
    }

    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "keyEvent: Server returned an error: "
                      << responseVal.error_message();
        return std::nullopt;
    }

    if (!responseVal.has_client_state()) {
        FCITX_ERROR() << "keyEvent: Server returned no client state";
        return std::nullopt;
    }

    return responseVal.client_state();
}
