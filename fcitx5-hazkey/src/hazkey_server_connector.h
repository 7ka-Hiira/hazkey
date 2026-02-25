#ifndef HAZKEY_SERVER_CONNECTOR_H
#define HAZKEY_SERVER_CONNECTOR_H

#include <fcitx-utils/log.h>
#include <fcitx/text.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <optional>
#include <string>

#include "base.pb.h"
#include "commands.pb.h"

class HazkeyServerConnector {
   public:
    // HazkeyServerConnector();
    // ~HazkeyServerConnector();

    HazkeyServerConnector() {
        // kill_existing_hazkey_server();
        connectServer();
        FCITX_DEBUG() << "Connector initialized";
    };

    std::string getSocketPath();

    void connectServer();

    void startHazkeyServer(bool force_restart);

    std::optional<hazkey::ResponseEnvelope> transact(
        const hazkey::RequestEnvelope& send_data);

    void saveLearningData();

    std::optional<hazkey::commands::ClientState> processKeyEvent(
        const hazkey::commands::KeyEvent &event);

    struct CandidateData {
        std::string candidateText;
        std::string subHiragana;
    };

    hazkey::commands::ShowCandidates::Candidate getCandidates(bool isSuggest);
    void resetServerState(bool completedPreedit);

   private:
    bool retryConnect();
    bool isHazkeyServerRunning();
    bool requestSuccess(hazkey::ResponseEnvelope);
    int sock_ = -1;
    std::string socket_path_;
};

#endif  // HAZKEY_SERVER_CONNECTOR_H
