#pragma once

#include "cloudflared/platform.hpp"
#include <string>

namespace cloudflared {

inline constexpr const char* PINNED_CLOUDFLARED_VERSION = "2026.8.3";

struct ReleaseAsset {
    std::string version;
    std::string filename;
    std::string download_url;
    std::string sha256;
};

/**
 * @brief Retrieves release asset metadata including official SHA256 checksum.
 *
 * @param platform Resolved platform information.
 * @param version Cloudflared version (defaults to pinned version).
 * @return ReleaseAsset Metadata for downloading and verifying the binary.
 */
ReleaseAsset get_release_asset(const PlatformInfo& platform, const std::string& version = PINNED_CLOUDFLARED_VERSION);

} // namespace cloudflared
