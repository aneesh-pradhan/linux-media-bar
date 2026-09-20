#include "content/content_directory.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace lmb::content {
namespace {

std::string xml_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += character; break;
        }
    }
    return result;
}

std::string percent_encode(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const char raw_character : value) {
        const unsigned char character = static_cast<unsigned char>(raw_character);
        if (std::isalnum(character) != 0 || character == '-' || character == '_' ||
            character == '.' || character == '~' || character == '/') {
            result += static_cast<char>(character);
        } else {
            result += '%';
            result += hex[character >> 4U];
            result += hex[character & 0x0FU];
        }
    }
    return result;
}

bool is_video_file(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    return extension == ".mkv" || extension == ".mp4" || extension == ".avi" ||
           extension == ".m2ts" || extension == ".ts";
}

std::string display_name(const std::filesystem::path& path) {
    return path.filename().string();
}

} // namespace

ContentDirectory::ContentDirectory(const network::SsdpConfig& config)
    : config_(config), media_root_(std::filesystem::path("/home/aneeshpradhan/Videos")) {}

std::string ContentDirectory::encode_id(const std::string& kind,
                                        const std::filesystem::path& relative) const {
    if (relative.empty() || relative == ".") {
        return kind;
    }
    return kind + ":" + percent_encode(relative.generic_string());
}

bool ContentDirectory::decode_id(const std::string& object_id, std::string& kind,
                                 std::filesystem::path& relative) const {
    if (object_id == "0") {
        kind = "root";
        return true;
    }
    if (object_id == "anime") {
        kind = "anime";
        return true;
    }
    const auto separator = object_id.find(':');
    if (separator == std::string::npos ||
        (object_id.substr(0, separator) != "dir" && object_id.substr(0, separator) != "file")) {
        return false;
    }
    kind = object_id.substr(0, separator);
    const std::string encoded = object_id.substr(separator + 1);
    std::string decoded;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            const auto hex_value = [](char character) -> int {
                if (character >= '0' && character <= '9') return character - '0';
                if (character >= 'A' && character <= 'F') return character - 'A' + 10;
                if (character >= 'a' && character <= 'f') return character - 'a' + 10;
                return -1;
            };
            const int high = hex_value(encoded[i + 1]);
            const int low = hex_value(encoded[i + 2]);
            if (high < 0 || low < 0) return false;
            decoded += static_cast<char>((high << 4) | low);
            i += 2;
        } else {
            decoded += encoded[i];
        }
    }
    relative = std::filesystem::path(decoded);
    return !relative.is_absolute() &&
           std::find(relative.begin(), relative.end(), "..") == relative.end();
}

std::string ContentDirectory::browse_didl(const std::string& object_id, const std::string& browse_flag,
                                          std::uint32_t starting_index, std::uint32_t requested_count,
                                          std::uint32_t& number_returned,
                                          std::uint32_t& total_matches) const {
    struct Entry { std::string id; std::string title; bool directory; std::uintmax_t size; };
    std::vector<Entry> entries;
    std::string kind;
    std::filesystem::path relative;
    if (!decode_id(object_id, kind, relative)) {
        return {};
    }

    if (browse_flag == "BrowseMetadata") {
        if (object_id == "0") {
            entries.push_back({"0", "Aneesh Anime Server", true, 0});
        } else if (object_id == "anime") {
            entries.push_back({"anime", "Anime", true, 0});
        } else {
            const auto path = media_root_ / relative;
            std::error_code error;
            if (std::filesystem::is_directory(path, error)) {
                entries.push_back({object_id, display_name(path), true, 0});
            } else if (std::filesystem::is_regular_file(path, error) && is_video_file(path)) {
                entries.push_back({object_id, display_name(path), false, std::filesystem::file_size(path, error)});
            }
        }
    } else if (browse_flag == "BrowseDirectChildren") {
        if (object_id == "0") {
            entries.push_back({"anime", "Anime", true, 0});
        } else {
            const auto directory = object_id == "anime" ? media_root_ : media_root_ / relative;
            std::error_code error;
            const auto root = std::filesystem::weakly_canonical(media_root_, error);
            const auto canonical_directory = std::filesystem::weakly_canonical(directory, error);
            if (error || canonical_directory.string().compare(0, root.string().size(), root.string()) != 0) {
                return {};
            }
            for (const auto& entry : std::filesystem::directory_iterator(canonical_directory, error)) {
                if (error) break;
                const auto relative_entry = std::filesystem::relative(entry.path(), root, error);
                if (error) continue;
                if (entry.is_directory(error)) {
                    entries.push_back({encode_id("dir", relative_entry), display_name(entry.path()), true, 0});
                } else if (entry.is_regular_file(error) && is_video_file(entry.path())) {
                    entries.push_back({encode_id("file", relative_entry), display_name(entry.path()), false,
                                       entry.file_size(error)});
                }
            }
            std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
                return left.title < right.title;
            });
        }
    }

    total_matches = static_cast<std::uint32_t>(entries.size());
    const std::size_t start = std::min<std::size_t>(starting_index, entries.size());
    const std::size_t count = requested_count == 0
        ? entries.size() - start
        : std::min<std::size_t>(requested_count, entries.size() - start);
    number_returned = static_cast<std::uint32_t>(count);

    std::ostringstream didl;
    didl << R"(<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/" xmlns:dlna="urn:schemas-dlna-org:metadata-1-0/")";
    didl << '>';
    for (std::size_t i = start; i < start + count; ++i) {
        const Entry& entry = entries[i];
        if (entry.directory) {
            didl << "<container id=\"" << xml_escape(entry.id) << "\" parentID=\"" << xml_escape(object_id)
                 << "\" restricted=\"1\"><dc:title>" << xml_escape(entry.title)
                 << "</dc:title><upnp:class>object.container.storageFolder</upnp:class></container>";
        } else {
            const std::string url = "http://" + config_.bind_address + ":" + std::to_string(config_.http_port) +
                                    "/media/" + percent_encode(entry.id.substr(5));
            didl << "<item id=\"" << xml_escape(entry.id) << "\" parentID=\"" << xml_escape(object_id)
                 << "\" restricted=\"1\"><dc:title>" << xml_escape(entry.title)
                 << "</dc:title><upnp:class>object.item.videoItem</upnp:class><res protocolInfo=\"http-get:*:video/x-matroska:*\" size=\""
                 << entry.size << "\">" << xml_escape(url) << "</res></item>";
        }
    }
    didl << "</DIDL-Lite>";
    return didl.str();
}

std::string ContentDirectory::browse_response(const std::string& object_id, const std::string& browse_flag,
                                              std::uint32_t starting_index,
                                              std::uint32_t requested_count) const {
    std::uint32_t number_returned = 0;
    std::uint32_t total_matches = 0;
    const std::string didl = browse_didl(object_id, browse_flag, starting_index, requested_count,
                                         number_returned, total_matches);
    std::ostringstream response;
    response << R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"><Result>)";
    response << xml_escape(didl);
    response << "</Result><NumberReturned>" << number_returned << "</NumberReturned><TotalMatches>"
             << total_matches << "</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse></s:Body></s:Envelope>";
    return response.str();
}

} // namespace lmb::content
