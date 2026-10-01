#pragma once

#include "cloudflared/platform.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace cloudflared {

/**
 * @brief Returns the base cache directory for the current user and platform.
 * Respects the CLOUDFLARED_CACHE_DIR environment variable if set.
 */
std::filesystem::path get_default_cache_directory();

/**
 * @brief Resolves the target binary path within the cache structure:
 * <cache_dir>/cloudflared/<version>/<platform>-<arch>/<binary_name>
 *
 * @param platform Resolved platform information.
 * @param version Binary version string.
 * @param custom_cache_dir Optional user-provided cache directory override.
 * @return std::filesystem::path Absolute path where the binary should reside.
 */
std::filesystem::path resolve_binary_cache_path(
    const PlatformInfo& platform,
    const std::string& version,
    const std::optional<std::filesystem::path>& custom_cache_dir = std::nullopt
);

/**
 * @brief Generates a unique temporary download file path in the target directory.
 */
std::filesystem::path get_temp_download_path(const std::filesystem::path& target_binary_path);

} // namespace cloudflared
