#pragma once

#include "content/content_directory.h"
#include "network/ssdp/ssdp_server.h"

#include <atomic>

namespace lmb::network {

class HttpServer {
public:
    explicit HttpServer(const SsdpConfig& config);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void run();
    void stop();

private:
    void handle_client(int client_fd);

    SsdpConfig config_;
    content::ContentDirectory content_directory_;
    int socket_fd_ = -1;
    std::atomic<bool> stopping_{false};
};

} // namespace lmb::network
