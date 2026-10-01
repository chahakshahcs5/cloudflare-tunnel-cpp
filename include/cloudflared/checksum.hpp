#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace cloudflared {

/**
 * @brief Computes the SHA256 digest of a memory buffer and returns it as a lowercase hex string.
 */
std::string compute_sha256_hex(std::string_view data);

/**
 * @brief Computes the SHA256 digest of a file in streaming chunks (e.g. 64KB)
 * without loading the entire file into memory.
 *
 * @param path Path to the file.
 * @return std::string Lowercase 64-character hex string.
 * @throws std::runtime_error If the file cannot be opened.
 */
std::string compute_file_sha256_hex(const std::filesystem::path& path);

/**
 * @brief Verifies that a file's SHA256 matches the expected lowercase hex string.
 *
 * @param path Path to the file.
 * @param expected_sha256_hex Expected 64-character SHA256 hex string.
 * @return bool True if exact match (case-insensitive comparison).
 */
bool verify_file_sha256(const std::filesystem::path& path, const std::string& expected_sha256_hex);

} // namespace cloudflared
