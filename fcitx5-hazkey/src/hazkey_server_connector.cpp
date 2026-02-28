#include "hazkey_server_connector.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/text.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "base.pb.h"
#include "commands.pb.h"

static std::mutex transact_mutex;

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
        ssize_t n = read(fd, (char*)data + recved, len - recved);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(fd, &rfds);
                timeval tv = {10, 0};  // 2sec timeout
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

void HazkeyServerConnector::connectServer() {
    std::string socket_path = getSocketPath();

    // try restarting server only 1 time
    // on 1st attempt (minus 1)
    constexpr int ATTEMPT_TRY_START = 0;
    // on 4th attempt (minus 1)
    constexpr int ATTEMPT_TRY_START_FORCE = 3;

    constexpr int MAX_RETRIES = 8;
    constexpr int RETRY_INTERVAL_MS = 150;

    int attempt;
    for (attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        sock_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_ < 0) {
            FCITX_ERROR() << "Failed to create socket";
            std::this_thread::sleep_for(
                std::chrono::milliseconds(RETRY_INTERVAL_MS));
            continue;
        }
        int fcntlRes =
            fcntl(sock_, F_SETFL, fcntl(sock_, F_GETFL, 0) | O_NONBLOCK);
        if (fcntlRes != 0) {
            FCITX_ERROR() << "fcntl() failed";
            close(sock_);
            sock_ = -1;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(RETRY_INTERVAL_MS));
            continue;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        int ret = connect(sock_, (sockaddr*)&addr, sizeof(addr));
        if (ret == 0) {
            // Connected
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
                    // Connected
                    return;
                }
            }
        }
        FCITX_INFO() << "Failed to connect hazkey-server, retry "
                     << (attempt + 1);
        close(sock_);
        sock_ = -1;
        if (attempt == ATTEMPT_TRY_START) {
            startHazkeyServer(false);
        } else if (attempt == ATTEMPT_TRY_START_FORCE) {
            startHazkeyServer(true);
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(RETRY_INTERVAL_MS));
    }
    FCITX_INFO() << "Failed to connect hazkey-server after " << MAX_RETRIES
                 << " attempts";
}

std::optional<hazkey::ResponseEnvelope> HazkeyServerConnector::transact(
    const hazkey::RequestEnvelope& send_data, bool tryConnect) {
    std::lock_guard<std::mutex> lock(transact_mutex);

    if (sock_ == -1) {
        if (!tryConnect) {
            FCITX_INFO() << "Socket not connected. Aborting transact.";
            return std::nullopt;
        }
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

    FCITX_DEBUG() << "Sending message of size: " << msg.size();

    // write length
    uint32_t writeLen = htonl(msg.size());
    if (!writeAll(sock_, &writeLen, 4)) {
        close(sock_);
        sock_ = -1;
        if (tryConnect) {
            FCITX_INFO()
                << "Failed to communicate with server while writing data length. "
                   "reconnecting to hazkey-server...";
            
            connectServer();
        }
        return std::nullopt;
    }

    // write data
    if (!writeAll(sock_, msg.c_str(), msg.size())) {
        close(sock_);
        sock_ = -1;
        if (tryConnect) {
            FCITX_INFO() << "Failed to communicate with server while writing data. "
                            "reconnecting to hazkey-server...";
            connectServer();
        }
        return std::nullopt;
    }

    FCITX_DEBUG() << "Successfully wrote data to server";

    // read response length
    uint32_t readLenBuf;
    if (!readAll(sock_, &readLenBuf, 4)) {
        FCITX_ERROR() << "Failed to read buffer length.";
        close(sock_);
        sock_ = -1;
        return std::nullopt;
    }

    uint32_t readLen = ntohl(readLenBuf);
    FCITX_DEBUG() << "Server response size: " << readLen;

    if (readLen > 2 * 1024 * 1024) {  // 2MB limit
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
        FCITX_ERROR() << "Failed to parse received data\n";
        return std::nullopt;
    }

    FCITX_DEBUG() << "Successfully received and parsed response";
    return resp;
}

std::string HazkeyServerConnector::getComposingText(
    hazkey::commands::GetComposingString::CharType type,
    std::string currentPreedit) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_get_composing_string();
    props->set_char_type(type);
    props->set_current_preedit(currentPreedit);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting getComposingText().";
        return "";
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "getComposingText: " << "Server returned an error: "
                      << responseVal.error_message();
        return "";
    }
    // old protobuf doesn't have has_text() method.
    // if (!responseVal.has_text()) {
    //     FCITX_ERROR() << "getComposingText: "
    //                   << "Server returned unexpected response";
    //     return "";
    // }
    return responseVal.text();
}

fcitx::Text HazkeyServerConnector::getComposingHiraganaWithCursor() {
    hazkey::RequestEnvelope request;
    request.mutable_get_hiragana_with_cursor();
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR()
            << "Error while transacting getComposingHiraganaWithCursor().";
        return fcitx::Text();
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "getHiraganaWithCursor: "
                      << "Server returned an error: "
                      << responseVal.error_message();
        return fcitx::Text();
    }
    if (!responseVal.has_text_with_cursor()) {
        FCITX_ERROR() << "getHiraganaWithCursor: "
                      << "Server returned unexpected response";
        return fcitx::Text();
    }
    fcitx::Text text =
        fcitx::Text(responseVal.text_with_cursor().beforecursosr());
    text.append(responseVal.text_with_cursor().oncursor(),
                fcitx::TextFormatFlag::Underline);
    text.append(responseVal.text_with_cursor().aftercursor());
    return text;
}

void HazkeyServerConnector::inputChar(std::string text) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_input_char();
    props->set_text(text);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting inputChar().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "inputChar: " << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

void HazkeyServerConnector::shiftKeyEvent(bool isRelease) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_modifier_event();
    props->set_event_type(
        isRelease ? hazkey::commands::ModifierEvent_EventType_RELEASE
                  : hazkey::commands::ModifierEvent_EventType_PRESS);
    props->set_mod_type(hazkey::commands::ModifierEvent_ModifierType_SHIFT);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting shiftKeyEvent().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "shiftKeyEvent: " << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

bool HazkeyServerConnector::currentInputModeIsDirect() {
    hazkey::RequestEnvelope request;
    auto _ = request.mutable_get_current_input_mode();
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting currentInputModeIsDirect().";
        return false;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "currentInputModeIsDirect: "
                      << "Server returned an error: "
                      << responseVal.error_message();
        return false;
    }
    return responseVal.current_input_mode_info().input_mode() ==
           hazkey::commands::CurrentInputModeInfo::InputMode::
               CurrentInputModeInfo_InputMode_DIRECT;
}

void HazkeyServerConnector::deleteLeft() {
    hazkey::RequestEnvelope request;
    request.mutable_delete_left();
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting deleteLeft().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "deleteLeft: " << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

void HazkeyServerConnector::deleteRight() {
    hazkey::RequestEnvelope request;
    request.mutable_delete_right();
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting deleteRight().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "deleteRight: " << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

void HazkeyServerConnector::moveCursor(int offset) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_move_cursor();
    props->set_offset(offset);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting moveCursor().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "moveCursor:" << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

void HazkeyServerConnector::setContext(std::string context, int anchor) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_set_context();
    props->set_context(context);
    props->set_anchor(anchor);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting setContext().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "setContext:" << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

void HazkeyServerConnector::newComposingText() {
    hazkey::RequestEnvelope request;
    request.mutable_new_composing_text();
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR()
            << "Error while transacting createComposingTextInstance().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "createComposingTextInstance:"
                      << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

void HazkeyServerConnector::completePrefix(int index) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_prefix_complete();
    props->set_index(index);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting completePrefix().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "completePrefix: " << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

void HazkeyServerConnector::saveLearningData(bool tryConnect) {
    hazkey::RequestEnvelope request;
    request.mutable_save_learning_data();
    auto response = transact(request, tryConnect);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting saveLearningData().";
        return;
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "saveLearningData:"
                      << "Server returned an error: "
                      << responseVal.error_message();
        return;
    }
    return;
}

hazkey::commands::CandidatesResult HazkeyServerConnector::getCandidates(
    bool isSuggestMode) {
    hazkey::RequestEnvelope request;
    auto props = request.mutable_get_candidates();
    props->set_is_suggest(isSuggestMode);
    auto response = transact(request);
    if (response == std::nullopt) {
        FCITX_ERROR() << "Error while transacting setServerConfig().";
        std::vector<CandidateData> empty_vec;
        return hazkey::commands::CandidatesResult();
    }
    auto responseVal = response.value();
    if (responseVal.status() != hazkey::SUCCESS) {
        FCITX_ERROR() << "getCandidates: " << "Server returned an error: "
                      << responseVal.error_message();
        std::vector<CandidateData> empty_vec;
        return hazkey::commands::CandidatesResult();
    }
    // TODO: Error handling when response has no candidate
    // if (responseVal..has_candidates()) {
    //     FCITX_ERROR() << "getCandidates: "
    //                   << "Server returned unexpected response";
    //     std::vector<CandidateData> empty_vec;
    //     return hazkey::commands::CandidatesResult();
    // }
    return responseVal.candidates();
}
