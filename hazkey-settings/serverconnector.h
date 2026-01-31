#ifndef SERVERCONNECTOR_H
#define SERVERCONNECTOR_H

#include <optional>
#include <string>

#include "base.pb.h"

class ServerConnector {
   public:
    ServerConnector();
    ~ServerConnector();
    std::optional<hazkey::config::CurrentConfig> getConfig();
    void setCurrentConfig(hazkey::config::CurrentConfig);
    bool clearAllHistory(const std::string& profileId);
    bool reloadZenzaiModel();

    // Begin a session with persistent connection
    bool beginSession();
    // End the session and close connection
    void endSession();
    // Session-aware versions of methods
    std::optional<hazkey::config::CurrentConfig> getConfigInSession();
    bool reloadZenzaiModelInSession();

   private:
    std::string get_socket_path();
    void kill_existing_hazkey_server();
    int create_connection();
    std::optional<hazkey::ResponseEnvelope> transact(
        const hazkey::RequestEnvelope& send_data);
    std::optional<hazkey::ResponseEnvelope> transact_on_socket(
        int sock, const hazkey::RequestEnvelope& send_data);

    int session_socket_;
};

#endif  // SERVERCONNECTOR_H
