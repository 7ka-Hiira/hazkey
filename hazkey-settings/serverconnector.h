#ifndef SERVERCONNECTOR_H
#define SERVERCONNECTOR_H

#include <optional>
#include <string>

#include "base.pb.h"

class ServerConnector {
   public:
    ServerConnector();
    std::optional<hazkey::config::CurrentConfig> getConfig();
    void setCurrentConfig(hazkey::config::CurrentConfig);
    bool clearAllHistory(const std::string& profileId);
    bool reloadZenzaiModel();

   private:
    std::string get_socket_path();
    void kill_existing_hazkey_server();
    int create_connection();
    std::optional<hazkey::ResponseEnvelope> transact(
        const hazkey::RequestEnvelope& send_data);
};

#endif  // SERVERCONNECTOR_H
