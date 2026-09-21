#include "network/http/http_server.h"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace lmb::network {
namespace {

constexpr char kContentDirectoryType[] = "urn:schemas-upnp-org:service:ContentDirectory:1";
constexpr char kConnectionManagerType[] = "urn:schemas-upnp-org:service:ConnectionManager:1";

std::string xml_escape(std::string value) {
    std::size_t position = 0;
    while ((position = value.find('&', position)) != std::string::npos) {
        value.replace(position, 1, "&amp;");
        position += 5;
    }
    position = 0;
    while ((position = value.find('<', position)) != std::string::npos) {
        value.replace(position, 1, "&lt;");
        position += 4;
    }
    position = 0;
    while ((position = value.find('>', position)) != std::string::npos) {
        value.replace(position, 1, "&gt;");
        position += 4;
    }
    return value;
}

std::string device_description(const SsdpConfig& config) {
    const std::string uuid = "uuid:" + config.uuid;
    const std::string base = "http://" + config.bind_address + ":" + std::to_string(config.http_port);
    std::ostringstream xml;
    xml << R"(<?xml version="1.0" encoding="utf-8"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <URLBase>)" << base << R"(</URLBase>
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
    <friendlyName>)" << xml_escape(config.friendly_name) << R"(</friendlyName>
    <manufacturer>Linux Media Bar</manufacturer>
    <manufacturerURL>https://github.com/</manufacturerURL>
    <modelDescription>PS3-oriented local anime media server</modelDescription>
    <modelName>Linux Media Bar</modelName>
    <modelNumber>0.1</modelNumber>
    <serialNumber>linux-media-bar</serialNumber>
    <UDN>)" << uuid << R"(</UDN>
    <serviceList>
      <service>
        <serviceType>)" << kContentDirectoryType << R"(</serviceType>
        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>
        <SCPDURL>/scpd/content_directory.xml</SCPDURL>
        <controlURL>/upnp/control/content_directory</controlURL>
        <eventSubURL>/upnp/event/content_directory</eventSubURL>
      </service>
      <service>
        <serviceType>)" << kConnectionManagerType << R"(</serviceType>
        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>
        <SCPDURL>/scpd/connection_manager.xml</SCPDURL>
        <controlURL>/upnp/control/connection_manager</controlURL>
        <eventSubURL>/upnp/event/connection_manager</eventSubURL>
      </service>
    </serviceList>
  </device>
</root>
  )";
    return xml.str();
}

std::string content_directory_scpd() {
    return R"(<?xml version="1.0" encoding="utf-8"?>
<scpd xmlns="urn:schemas-upnp-org:service-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <actionList>
    <action><name>Browse</name><argumentList>
      <argument><name>ObjectID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument>
      <argument><name>BrowseFlag</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable></argument>
      <argument><name>Filter</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument>
      <argument><name>StartingIndex</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument>
      <argument><name>RequestedCount</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>
      <argument><name>SortCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument>
      <argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument>
      <argument><name>NumberReturned</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>
      <argument><name>TotalMatches</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>
      <argument><name>UpdateID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument>
    </argumentList></action>
    <action><name>GetSearchCapabilities</name><argumentList><argument><name>SearchCaps</name><direction>out</direction><relatedStateVariable>SearchCapabilities</relatedStateVariable></argument></argumentList></action>
    <action><name>GetSortCapabilities</name><argumentList><argument><name>SortCaps</name><direction>out</direction><relatedStateVariable>SortCapabilities</relatedStateVariable></argument></argumentList></action>
    <action><name>GetSystemUpdateID</name><argumentList><argument><name>Id</name><direction>out</direction><relatedStateVariable>SystemUpdateID</relatedStateVariable></argument></argumentList></action>
  </actionList>
  <serviceStateTable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ObjectID</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_BrowseFlag</name><dataType>string</dataType><allowedValueList><allowedValue>BrowseMetadata</allowedValue><allowedValue>BrowseDirectChildren</allowedValue></allowedValueList></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_Filter</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_SortCriteria</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_UpdateID</name><dataType>ui4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>SearchCapabilities</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>SortCapabilities</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="yes"><name>SystemUpdateID</name><dataType>ui4</dataType></stateVariable>
  </serviceStateTable>
</scpd>
  )";
}

std::string connection_manager_scpd() {
    return R"(<?xml version="1.0" encoding="utf-8"?>
<scpd xmlns="urn:schemas-upnp-org:service-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <actionList>
    <action><name>GetProtocolInfo</name><argumentList>
      <argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>
      <argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>
    </argumentList></action>
    <action><name>GetCurrentConnectionIDs</name><argumentList><argument><name>ConnectionIDs</name><direction>out</direction><relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument></argumentList></action>
    <action><name>GetCurrentConnectionInfo</name><argumentList>
      <argument><name>ConnectionID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>
      <argument><name>RcsID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument>
      <argument><name>AVTransportID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument>
      <argument><name>ProtocolInfo</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument>
      <argument><name>PeerConnectionManager</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument>
      <argument><name>PeerConnectionID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>
      <argument><name>Direction</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument>
      <argument><name>Status</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument>
    </argumentList></action>
  </actionList>
  <serviceStateTable>
    <stateVariable sendEvents="no"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType></stateVariable>
  </serviceStateTable>
</scpd>
  )";
}

std::string http_response(int status, std::string_view reason, std::string_view type,
                          const std::string& body, bool head_only) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: " << type << "; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n";
    if (!head_only) {
        response << body;
    }
    return response.str();
}

std::string xml_unescape(std::string value) {
    const std::pair<std::string_view, std::string_view> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"},
    };
    for (const auto& [entity, replacement] : entities) {
        std::size_t position = 0;
        while ((position = value.find(entity, position)) != std::string::npos) {
            value.replace(position, entity.size(), replacement);
            position += replacement.size();
        }
    }
    return value;
}

std::string soap_value(const std::string& request, std::string_view tag) {
    const std::string marker = std::string(tag) + ">";
    const auto marker_position = request.find(marker);
    if (marker_position == std::string::npos) {
        return {};
    }
    const auto value_start = marker_position + marker.size();
    const auto value_end = request.find("</", value_start);
    if (value_end == std::string::npos || value_end < value_start) {
        return {};
    }
    return xml_unescape(request.substr(value_start, value_end - value_start));
}

std::uint32_t parse_uint(const std::string& value) {
    try {
        return static_cast<std::uint32_t>(std::stoul(value));
    } catch (...) {
        return 0;
    }
}

bool send_all(int socket_fd, const char* data, std::size_t length) {
    while (length > 0) {
        const ssize_t sent = send(socket_fd, data, length, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        data += sent;
        length -= static_cast<std::size_t>(sent);
    }
    return true;
}

std::string header_value(const std::string& request, std::string wanted) {
    std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::istringstream stream(request);
    std::string line;
    while (std::getline(stream, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (name == wanted) {
            const auto first = line.find_first_not_of(" \t", colon + 1);
            return first == std::string::npos ? std::string{} : line.substr(first);
        }
    }
    return {};
}

std::string media_type(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".mp4" || extension == ".m4v") return "video/mp4";
    if (extension == ".m2ts" || extension == ".ts") return "video/mp2t";
    if (extension == ".avi") return "video/x-msvideo";
    return "video/x-matroska";
}

bool parse_range(const std::string& value, std::uintmax_t size,
                 std::uintmax_t& start, std::uintmax_t& end) {
    if (!value.starts_with("bytes=") || size == 0) return false;
    const std::string spec = value.substr(6);
    const auto dash = spec.find('-');
    if (dash == std::string::npos || spec.find(',', dash) != std::string::npos) return false;
    try {
        if (dash == 0) {
            const auto suffix = static_cast<std::uintmax_t>(std::stoull(spec.substr(1)));
            if (suffix == 0) return false;
            start = suffix >= size ? 0 : size - suffix;
            end = size - 1;
        } else {
            start = static_cast<std::uintmax_t>(std::stoull(spec.substr(0, dash)));
            if (start >= size) return false;
            if (dash + 1 == spec.size()) {
                end = size - 1;
            } else {
                end = std::min<std::uintmax_t>(static_cast<std::uintmax_t>(std::stoull(spec.substr(dash + 1))), size - 1);
                if (end < start) return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

HttpServer::HttpServer(const SsdpConfig& config) : config_(config), content_directory_(config) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::run() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        throw std::runtime_error("socket(AF_INET, SOCK_STREAM): " + std::string(std::strerror(errno)));
    }
    int reuse = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.http_port);
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid HTTP bind address: " + config_.bind_address);
    }
    if (bind(socket_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(socket_fd_, 16) < 0) {
        throw std::runtime_error("HTTP bind/listen: " + std::string(std::strerror(errno)));
    }
    stopping_ = false;
    std::cout << "INFO HTTP listening on " << config_.bind_address << ':' << config_.http_port << "\n";

    while (!stopping_) {
        const int client = accept(socket_fd_, nullptr, nullptr);
        if (client < 0) {
            if (stopping_ || errno == EINTR) {
                continue;
            }
            std::cerr << "ERROR HTTP accept: " << std::strerror(errno) << '\n';
            continue;
        }
        std::thread(&HttpServer::handle_client, this, client).detach();
    }
}

void HttpServer::handle_media(int client_fd, const std::string& method,
                              const std::string& path, const std::string& range) {
    const std::string encoded_relative = path.substr(std::string("/media/").size());
    std::filesystem::path media_path;
    if (!content_directory_.resolve_media_path(encoded_relative, media_path)) {
        const std::string response = http_response(404, "Not Found", "text/plain", "Not Found\n", method == "HEAD");
        send_all(client_fd, response.data(), response.size());
        std::cout << "INFO HTTP " << method << ' ' << path << " -> 404\n";
        return;
    }

    struct stat file_stat{};
    const int file_fd = open(media_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file_fd < 0 || fstat(file_fd, &file_stat) < 0) {
        if (file_fd >= 0) close(file_fd);
        const std::string response = http_response(404, "Not Found", "text/plain", "Not Found\n", method == "HEAD");
        send_all(client_fd, response.data(), response.size());
        return;
    }
    const auto size = static_cast<std::uintmax_t>(file_stat.st_size);
    std::uintmax_t start = 0;
    std::uintmax_t end = size == 0 ? 0 : size - 1;
    const bool partial = !range.empty();
    if (partial && !parse_range(range, size, start, end)) {
        std::ostringstream response;
        response << "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" << size
                 << "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        const std::string output = response.str();
        send_all(client_fd, output.data(), output.size());
        close(file_fd);
        std::cout << "INFO HTTP " << method << ' ' << path << " -> 416\n";
        return;
    }

    const std::uintmax_t length = size == 0 ? 0 : end - start + 1;
    std::ostringstream response;
    response << "HTTP/1.1 " << (partial ? "206 Partial Content" : "200 OK") << "\r\n"
             << "Content-Type: " << media_type(media_path) << "\r\n"
             << "Content-Length: " << length << "\r\n"
             << "Accept-Ranges: bytes\r\n";
    if (partial) response << "Content-Range: bytes " << start << '-' << end << '/' << size << "\r\n";
    response << "Connection: close\r\n\r\n";
    const std::string headers = response.str();
    bool ok = send_all(client_fd, headers.data(), headers.size());
    if (ok && method != "HEAD" && length > 0) {
        if (lseek(file_fd, static_cast<off_t>(start), SEEK_SET) < 0) {
            ok = false;
        } else {
            std::array<char, 64 * 1024> buffer{};
            std::uintmax_t remaining = length;
            while (ok && remaining > 0) {
                const std::size_t wanted = static_cast<std::size_t>(std::min<std::uintmax_t>(remaining, buffer.size()));
                const ssize_t received = read(file_fd, buffer.data(), wanted);
                if (received <= 0) { ok = false; break; }
                ok = send_all(client_fd, buffer.data(), static_cast<std::size_t>(received));
                remaining -= static_cast<std::size_t>(received);
            }
        }
    }
    close(file_fd);
    std::cout << "INFO HTTP " << method << ' ' << path << " -> " << (partial ? 206 : 200)
              << (ok ? "\n" : " (client disconnected)\n");
}

void HttpServer::handle_transcode(int client_fd, const std::string& method, const std::string& path) {
    const std::string encoded_relative = path.substr(std::string("/transcode/").size());
    std::filesystem::path media_path;
    if (!content_directory_.resolve_media_path(encoded_relative, media_path)) {
        const std::string response = http_response(404, "Not Found", "text/plain", "Not Found\n", method == "HEAD");
        send_all(client_fd, response.data(), response.size());
        return;
    }
    const std::string headers =
        "HTTP/1.1 200 OK\r\nContent-Type: video/mpeg\r\n"
        "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    if (!send_all(client_fd, headers.data(), headers.size()) || method == "HEAD") return;

    int pipe_fds[2]{};
    if (pipe(pipe_fds) < 0) return;
    const pid_t child = fork();
    if (child == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        const std::string input = media_path.string();
        execlp("ffmpeg", "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", input.c_str(),
               "-map", "0:v:0", "-map", "0:a:0?", "-c:v", "libx264", "-profile:v", "high",
               "-level:v", "4.1", "-pix_fmt", "yuv420p", "-preset", "veryfast", "-c:a", "aac",
               "-b:a", "192k", "-f", "mpegts", "pipe:1", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(pipe_fds[1]);
    if (child < 0) {
        close(pipe_fds[0]);
        return;
    }

    std::array<char, 64 * 1024> buffer{};
    bool ok = true;
    while (ok) {
        const ssize_t received = read(pipe_fds[0], buffer.data(), buffer.size());
        if (received == 0) break;
        if (received < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        std::ostringstream chunk_header;
        chunk_header << std::hex << received << "\r\n";
        const std::string header = chunk_header.str();
        ok = send_all(client_fd, header.data(), header.size()) &&
             send_all(client_fd, buffer.data(), static_cast<std::size_t>(received)) &&
             send_all(client_fd, "\r\n", 2);
    }
    close(pipe_fds[0]);
    if (!ok) kill(child, SIGTERM);
    int child_status = 0;
    waitpid(child, &child_status, 0);
    if (ok) send_all(client_fd, "0\r\n\r\n", 7);
    std::cout << "INFO HTTP " << method << ' ' << path << " -> 200 transcode"
              << (ok ? "\n" : " (client disconnected)\n");
}

void HttpServer::handle_client(int client_fd) {
    char buffer[16384];
    const ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        close(client_fd);
        return;
    }
    buffer[received] = '\0';
    const std::string request(buffer, static_cast<std::size_t>(received));
    const auto line_end = request.find("\r\n");
    const std::string first_line = request.substr(0, line_end);
    std::istringstream line(first_line);
    std::string method;
    std::string path;
    std::string version;
    line >> method >> path >> version;
    const bool head_only = method == "HEAD";
    const std::string path_only = path.substr(0, path.find('?'));
    if (path_only.starts_with("/media/") && (method == "GET" || head_only)) {
        handle_media(client_fd, method, path_only, header_value(request, "range"));
        close(client_fd);
        return;
    }
    if (path_only.starts_with("/transcode/") && (method == "GET" || head_only)) {
        handle_transcode(client_fd, method, path_only);
        close(client_fd);
        return;
    }
    const bool soap_request = (method == "POST" || method == "M-POST") &&
                              (path_only == "/upnp/control/content_directory" ||
                               path_only == "/upnp/control/connection_manager");

    std::string body;
    std::string type = "text/plain";
    int status = 200;
    std::string_view reason = "OK";
    if (soap_request && path_only == "/upnp/control/content_directory") {
        const auto body_start = request.find("\r\n\r\n");
        const std::string soap_body = body_start == std::string::npos
            ? std::string{}
            : request.substr(body_start + 4);
        body = content_directory_.browse_response(
            soap_value(soap_body, "ObjectID").empty() ? "0" : soap_value(soap_body, "ObjectID"),
            soap_value(soap_body, "BrowseFlag").empty() ? "BrowseDirectChildren" : soap_value(soap_body, "BrowseFlag"),
            parse_uint(soap_value(soap_body, "StartingIndex")),
            parse_uint(soap_value(soap_body, "RequestedCount")));
        type = "text/xml";
    } else if (soap_request && path_only == "/upnp/control/connection_manager") {
        body = R"(<?xml version="1.0" encoding="utf-8"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:GetProtocolInfoResponse xmlns:u="urn:schemas-upnp-org:service:ConnectionManager:1"><Source>http-get:*:video/mp4:*</Source><Sink></Sink></u:GetProtocolInfoResponse></s:Body></s:Envelope>)";
        type = "text/xml";
    } else if (method != "GET" && !head_only) {
        status = 405;
        reason = "Method Not Allowed";
        body = "Method Not Allowed\n";
    } else if (path == "/device.xml") {
        type = "text/xml";
        body = device_description(config_);
    } else if (path == "/scpd/content_directory.xml") {
        type = "text/xml";
        body = content_directory_scpd();
    } else if (path == "/scpd/connection_manager.xml") {
        type = "text/xml";
        body = connection_manager_scpd();
    } else {
        status = 404;
        reason = "Not Found";
        body = "Not Found\n";
    }

    const std::string output = http_response(status, reason, type, body, head_only);
    send(client_fd, output.data(), output.size(), MSG_NOSIGNAL);
    close(client_fd);
    std::cout << "INFO HTTP " << method << ' ' << path << " -> " << status << '\n';
}

void HttpServer::stop() {
    const bool already_stopping = stopping_.exchange(true);
    if (!already_stopping && socket_fd_ >= 0) {
        shutdown(socket_fd_, SHUT_RDWR);
    }
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

} // namespace lmb::network
