#pragma once

#include "cloudflared/platform.hpp"
#include "cloudflared/releases.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace cloudflared {

/**
 * @brief Ensures the official cloudflared binary is installed and verified.
 *
 * Checks in order:
 * 1. custom_binary_path (if provided)
 * 2. CLOUDFLARED_BIN environment variable
 * 3. Local per-user cache directory
 *
 * If not cached or force_download is true:
 * - Downloads the release asset to a temporary file
 * - Performs mandatory SHA256 checksum verification
 * - Atomically moves verified binary into final cache location
 * - Sets executable permissions (chmod 0755) on Unix
 *
 * Thread-safe: concurrent calls share synchronization to prevent race conditions.
 *
 * @param custom_binary_path Optional explicit path to a cloudflared executable.
 * @param custom_cache_dir Optional explicit cache directory.
 * @param version Cloudflared release version to resolve (defaults to pinned 2026.8.3).
 * @param force_download If true, re-downloads even if already present.
 * @param progress_callback Optional download progress callback.
 * @return std::filesystem::path Path to the verified executable.
 * @throws BinaryVerificationError If checksum fails.
 * @throws BinaryDownloadError If download fails.
 */
std::filesystem::path ensure_cloudflared(
    const std::optional<std::filesystem::path>& custom_binary_path = std::nullopt,
    const std::optional<std::filesystem::path>& custom_cache_dir = std::nullopt,
    const std::string& version = PINNED_CLOUDFLARED_VERSION,
    bool force_download = false,
    std::function<void(size_t downloaded, size_t total)> progress_callback = nullptr,
    std::function<void(const std::string&)> log_callback = nullptr
);

/**
 * @brief Removes the cached binary for the given version.
 * @return bool True if a file was removed, false if it didn't exist.
 */
bool remove_cached_cloudflared(
    const std::optional<std::filesystem::path>& custom_cache_dir = std::nullopt,
    const std::string& version = PINNED_CLOUDFLARED_VERSION
);

} // namespace cloudflared
