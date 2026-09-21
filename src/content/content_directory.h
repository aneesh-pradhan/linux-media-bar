#pragma once

#include "network/ssdp/ssdp_server.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace lmb::content {

class ContentDirectory {
public:
    explicit ContentDirectory(const network::SsdpConfig& config);

    std::string browse_response(const std::string& object_id,
                                const std::string& browse_flag,
                                std::uint32_t starting_index,
                                std::uint32_t requested_count) const;

    bool resolve_media_path(const std::string& encoded_relative,
                            std::filesystem::path& path) const;

private:
    std::string browse_didl(const std::string& object_id, const std::string& browse_flag,
                            std::uint32_t starting_index, std::uint32_t requested_count,
                            std::uint32_t& number_returned, std::uint32_t& total_matches) const;
    std::string encode_id(const std::string& kind, const std::filesystem::path& relative) const;
    bool decode_id(const std::string& object_id, std::string& kind,
                   std::filesystem::path& relative) const;

    network::SsdpConfig config_;
    std::filesystem::path media_root_;
};

} // namespace lmb::content
