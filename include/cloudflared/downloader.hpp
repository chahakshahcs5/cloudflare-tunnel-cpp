#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace cloudflared {

/**
 * @brief Downloads a file over HTTPS directly to a destination path.
 * Follows HTTP redirects (301, 302, 303, 307, 308).
 * Streams data directly to disk in chunks to minimize memory consumption.
 *
 * @param url Remote HTTPS URL.
 * @param destination Local filesystem destination path.
 * @param progress_callback Optional callback receiving (downloaded_bytes, total_bytes).
 * @throws BinaryDownloadError On HTTP or network errors.
 */
void download_file(
    const std::string& url,
    const std::filesystem::path& destination,
    std::function<void(size_t downloaded, size_t total)> progress_callback = nullptr
);

/**
 * @brief Downloads text content from an HTTPS URL into a std::string.
 *
 * @param url Remote HTTPS URL.
 * @return std::string Response body.
 * @throws BinaryDownloadError On HTTP or network errors.
 */
std::string download_string(const std::string& url);

using CustomDownloader = std::function<bool(const std::string& url, const std::filesystem::path& destination, std::function<void(size_t, size_t)> progress_callback)>;

/**
 * @brief Sets a custom download provider (e.g. Android JNI HttpURLConnection).
 */
void set_custom_downloader(CustomDownloader downloader);

} // namespace cloudflared
