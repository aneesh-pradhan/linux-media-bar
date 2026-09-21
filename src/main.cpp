#include "network/http/http_server.h"
#include "network/ssdp/ssdp_server.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
lmb::network::SsdpServer* server = nullptr;

void handle_signal(int) {
    if (server != nullptr) {
        server->stop();
    }
}
}

int main(int argc, char** argv) {
    lmb::network::SsdpConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--ssdp-port" && i + 1 < argc) {
            config.ssdp_port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        } else if (argument == "--http-port" && i + 1 < argc) {
            config.http_port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        } else if (argument == "--media-root" && i + 1 < argc) {
            config.media_root = argv[++i];
        } else if (argument == "--help") {
            std::cout << "Usage: linux-media-bar [--ssdp-port PORT] [--http-port PORT] [--media-root PATH]\n";
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return EXIT_FAILURE;
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        lmb::network::SsdpServer ssdp(config);
        lmb::network::HttpServer http(config);
        server = &ssdp;
        std::thread http_thread([&http, &ssdp]() {
            try {
                http.run();
            } catch (const std::exception& error) {
                std::cerr << "ERROR HTTP thread: " << error.what() << '\n';
                ssdp.stop();
            }
        });
        ssdp.run();
        http.stop();
        http_thread.join();
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
