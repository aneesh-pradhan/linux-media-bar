#pragma once

#include "content/content_directory.h"
#include "network/ssdp/ssdp_server.h"

#include <atomic>
#include <filesystem>
#include <string>

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
    void handle_media(int client_fd, const std::string& method, const std::string& path,
                      const std::string& range);
    void handle_transcode(int client_fd, const std::string& method, const std::string& path);

    SsdpConfig config_;
    content::ContentDirectory content_directory_;
    int socket_fd_ = -1;
    std::atomic<bool> stopping_{false};
};

} // namespace lmb::network
