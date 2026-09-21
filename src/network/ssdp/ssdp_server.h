#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace lmb::network {

struct SsdpConfig {
    std::string interface_name = "enp42s0";
    std::string bind_address = "192.168.1.66";
    std::uint16_t ssdp_port = 1900;
    std::uint16_t http_port = 5001;
    std::string media_root = "/home/aneeshpradhan/Videos";
    std::string friendly_name = "Aneesh Anime Server";
    std::string uuid = "b8f32da0-4f80-4d0c-a376-9b4f8dbb6b01";
};

class SsdpServer {
public:
    explicit SsdpServer(SsdpConfig config);
    ~SsdpServer();

    SsdpServer(const SsdpServer&) = delete;
    SsdpServer& operator=(const SsdpServer&) = delete;

    void run();
    void stop();

private:
    void receive_loop();
    void notify_loop();

    SsdpConfig config_;
    int socket_fd_ = -1;
    std::atomic<bool> stopping_{false};
    std::thread notify_thread_;
};

} // namespace lmb::network
