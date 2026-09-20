#include "network/ssdp/ssdp_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lmb::network {
namespace {

constexpr char kMulticastAddress[] = "239.255.255.250";
constexpr char kRootDevice[] = "upnp:rootdevice";
constexpr char kMediaServerType[] = "urn:schemas-upnp-org:device:MediaServer:1";
constexpr char kContentDirectoryType[] = "urn:schemas-upnp-org:service:ContentDirectory:1";

std::string lower(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string header_value(const std::string& request, const std::string& wanted) {
    std::istringstream stream(request);
    std::string line;
    const std::string wanted_lower = lower(wanted);
    while (std::getline(stream, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        if (lower(trim(line.substr(0, colon))) == wanted_lower) {
            return trim(line.substr(colon + 1));
        }
    }
    return {};
}

std::string rfc1123_now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%a, %d %b %Y %H:%M:%S GMT");
    return output.str();
}

std::string device_uuid(const SsdpConfig& config) {
    return "uuid:" + config.uuid;
}

std::string device_location(const SsdpConfig& config) {
    return "http://" + config.bind_address + ":" + std::to_string(config.http_port) + "/device.xml";
}

std::string usn_for(const SsdpConfig& config, const std::string& search_target) {
    const std::string uuid = device_uuid(config);
    if (search_target == kRootDevice) {
        return uuid + "::" + kRootDevice;
    }
    if (search_target == kMediaServerType) {
        return uuid + "::" + kMediaServerType;
    }
    if (search_target == kContentDirectoryType) {
        return uuid + "::" + kContentDirectoryType;
    }
    return uuid;
}

std::string response(const SsdpConfig& config, const std::string& search_target) {
    std::ostringstream output;
    output << "HTTP/1.1 200 OK\r\n"
           << "CACHE-CONTROL: max-age=1800\r\n"
           << "DATE: " << rfc1123_now() << "\r\n"
           << "EXT:\r\n"
           << "LOCATION: " << device_location(config) << "\r\n"
           << "SERVER: Linux/" << config.friendly_name << " UPnP/1.1 LinuxMediaBar/0.1\r\n"
           << "ST: " << search_target << "\r\n"
           << "USN: " << usn_for(config, search_target) << "\r\n"
           << "\r\n";
    return output.str();
}

std::string notify(const SsdpConfig& config, const std::string& notification_type, const std::string& usn) {
    std::ostringstream output;
    output << "NOTIFY * HTTP/1.1\r\n"
           << "HOST: " << kMulticastAddress << ":1900\r\n"
           << "CACHE-CONTROL: max-age=1800\r\n"
           << "LOCATION: " << device_location(config) << "\r\n"
           << "NT: " << notification_type << "\r\n"
           << "NTS: ssdp:alive\r\n"
           << "SERVER: Linux/" << config.friendly_name << " UPnP/1.1 LinuxMediaBar/0.1\r\n"
           << "USN: " << usn << "\r\n\r\n";
    return output.str();
}

bool is_m_search(const std::string& request) {
    const auto first_line_end = request.find("\r\n");
    const std::string first_line = request.substr(0, first_line_end);
    return lower(first_line).starts_with("m-search ");
}

} // namespace

SsdpServer::SsdpServer(SsdpConfig config) : config_(std::move(config)) {}

SsdpServer::~SsdpServer() {
    stop();
}

void SsdpServer::run() {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        throw std::runtime_error("socket(AF_INET, SOCK_DGRAM): " + std::string(std::strerror(errno)));
    }

    if (setsockopt(socket_fd_, SOL_SOCKET, SO_BINDTODEVICE, config_.interface_name.c_str(),
                   static_cast<socklen_t>(config_.interface_name.size() + 1)) < 0) {
        throw std::runtime_error("setsockopt(SO_BINDTODEVICE " + config_.interface_name + "): " +
                                 std::string(std::strerror(errno)));
    }

    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(config_.ssdp_port);
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_fd_, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) < 0) {
        throw std::runtime_error("bind(SSDP " + config_.bind_address + ":" +
                                 std::to_string(config_.ssdp_port) + "): " + std::string(std::strerror(errno)));
    }

    const unsigned int interface_index = if_nametoindex(config_.interface_name.c_str());
    if (interface_index == 0) {
        throw std::runtime_error("interface not found: " + config_.interface_name);
    }

    ip_mreqn membership{};
    if (inet_pton(AF_INET, kMulticastAddress, &membership.imr_multiaddr) != 1 ||
        inet_pton(AF_INET, config_.bind_address.c_str(), &membership.imr_address) != 1) {
        throw std::runtime_error("failed to configure SSDP multicast address");
    }
    membership.imr_ifindex = static_cast<int>(interface_index);
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0) {
        throw std::runtime_error("setsockopt(IP_ADD_MEMBERSHIP): " + std::string(std::strerror(errno)));
    }

    ip_mreqn multicast_interface{};
    multicast_interface.imr_ifindex = static_cast<int>(interface_index);
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_IF, &multicast_interface,
                   sizeof(multicast_interface)) < 0) {
        throw std::runtime_error("setsockopt(IP_MULTICAST_IF): " + std::string(std::strerror(errno)));
    }

    std::cout << "INFO SSDP listening on " << config_.bind_address << ':' << config_.ssdp_port
              << " via " << config_.interface_name << "\n";
    std::cout << "INFO device description will be " << device_location(config_) << "\n";
    stopping_ = false;
    notify_thread_ = std::thread(&SsdpServer::notify_loop, this);
    receive_loop();
}

void SsdpServer::receive_loop() {
    char buffer[8192];
    while (!stopping_) {
        sockaddr_in sender{};
        socklen_t sender_length = sizeof(sender);
        const ssize_t received = recvfrom(socket_fd_, buffer, sizeof(buffer) - 1, 0,
                                          reinterpret_cast<sockaddr*>(&sender), &sender_length);
        if (received < 0) {
            if (stopping_ || errno == EINTR) {
                continue;
            }
            std::cerr << "ERROR SSDP recvfrom: " << std::strerror(errno) << '\n';
            continue;
        }
        buffer[received] = '\0';
        const std::string request(buffer, static_cast<std::size_t>(received));
        if (!is_m_search(request)) {
            continue;
        }

        const std::string search_target = lower(header_value(request, "ST"));
        std::vector<std::string> targets;
        if (search_target == "ssdp:all") {
            targets = {kRootDevice, kMediaServerType, kContentDirectoryType};
        } else if (search_target == lower(kRootDevice) ||
                   search_target == lower(kMediaServerType) ||
                   search_target == lower(kContentDirectoryType) ||
                   search_target == lower(device_uuid(config_))) {
            targets = {header_value(request, "ST")};
        } else {
            continue;
        }

        for (const std::string& target : targets) {
            const std::string reply = response(config_, target);
            if (sendto(socket_fd_, reply.data(), reply.size(), 0,
                       reinterpret_cast<const sockaddr*>(&sender), sender_length) < 0) {
                std::cerr << "ERROR SSDP sendto: " << std::strerror(errno) << '\n';
            }
        }
        char sender_address[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sender.sin_addr, sender_address, sizeof(sender_address));
        std::cout << "INFO SSDP M-SEARCH from " << sender_address << ':' << ntohs(sender.sin_port)
                  << " ST=" << (search_target.empty() ? "<missing>" : search_target) << '\n';
    }
}

void SsdpServer::notify_loop() {
    sockaddr_in multicast_destination{};
    multicast_destination.sin_family = AF_INET;
    multicast_destination.sin_port = htons(1900);
    inet_pton(AF_INET, kMulticastAddress, &multicast_destination.sin_addr);

    while (!stopping_) {
        const std::string uuid = device_uuid(config_);
        const std::pair<std::string, std::string> announcements[] = {
            {"upnp:rootdevice", uuid + "::upnp:rootdevice"},
            {uuid, uuid},
            {kMediaServerType, uuid + "::" + kMediaServerType},
            {kContentDirectoryType, uuid + "::" + kContentDirectoryType},
        };
        for (const auto& [type, usn] : announcements) {
            const std::string packet = notify(config_, type, usn);
            sendto(socket_fd_, packet.data(), packet.size(), 0,
                   reinterpret_cast<const sockaddr*>(&multicast_destination), sizeof(multicast_destination));
        }
        for (int i = 0; i < 30 && !stopping_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void SsdpServer::stop() {
    const bool already_stopping = stopping_.exchange(true);
    if (!already_stopping && socket_fd_ >= 0) {
        shutdown(socket_fd_, SHUT_RDWR);
    }
    if (notify_thread_.joinable()) {
        notify_thread_.join();
    }
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

} // namespace lmb::network
