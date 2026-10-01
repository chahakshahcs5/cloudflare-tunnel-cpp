#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cloudflared {

/**
 * @brief Log level controlling library output verbosity.
 */
enum class LogLevel {
    Silent, ///< No log output to standard streams
    Info,   ///< Informational messages (startup, public URL, shutdown)
    Debug   ///< Verbose output including raw cloudflared stdout/stderr lines
};

/**
 * @brief Represents an active edge connection established by cloudflared.
 */
struct Connection {
    std::string id;       ///< UUID of the connection
    std::string ip;       ///< Edge IP address
    std::string location; ///< Datacenter / region location code (e.g. "tpe01", "ord02")
    int index = -1;       ///< Connection index (connIndex)
};

/**
 * @brief Configuration options for creating a Cloudflare tunnel.
 */
struct TunnelOptions {
    /**
     * @brief Port number of the local HTTP server to expose.
     * Normalized internally to "http://localhost:<port>".
     * Must be between 1 and 65535.
     */
    std::optional<uint16_t> port;

    /**
     * @brief Local URL to expose, e.g. "http://localhost:8080" or "http://127.0.0.1:3000".
     */
    std::optional<std::string> url;

    /**
     * @brief Maximum duration to wait for the tunnel public URL before timing out.
     * Defaults to 30 seconds.
     */
    std::chrono::milliseconds timeout{30000};

    /**
     * @brief Logging level. Defaults to Silent.
     */
    LogLevel log_level = LogLevel::Silent;

    /**
     * @brief Optional override for the binary cache directory.
     * If not specified, uses CLOUDFLARED_CACHE_DIR or the OS standard cache directory.
     */
    std::optional<std::filesystem::path> cache_dir;

    /**
     * @brief Optional override to use a specific pre-existing cloudflared executable.
     */
    std::optional<std::filesystem::path> custom_binary_path;

    /**
     * @brief Additional CLI arguments to pass to the cloudflared process.
     */
    std::vector<std::string> extra_args;

    /**
     * @brief Optional Cloudflare Tunnel token for running a pre-configured named tunnel.
     * When provided, runs 'cloudflared tunnel run --token <token>'.
     */
    std::optional<std::string> token;

    /**
     * @brief Edge connection protocol: "auto", "quic", or "http2".
     * Using "http2" forces TCP, which bypasses firewalls/VPNs blocking UDP/QUIC.
     * Defaults to "http2" for maximum network and firewall compatibility.
     */
    std::optional<std::string> protocol{"http2"};

    /**
     * @brief Optional callback for structured log output from the library.
     * When set, all log messages (download progress, verification, tunnel status)
     * are routed through this callback instead of printing to std::cout.
     * This allows consumers (e.g., the MachineBridge server) to integrate tunnel
     * logs into their own logging infrastructure.
     * Signature: void(LogLevel level, const std::string& message)
     */
    std::function<void(LogLevel, const std::string&)> log_callback;
};

/**
 * @brief Validates tunnel options and normalizes the target URL.
 *
 * @param options User supplied options.
 * @return std::string Normalized URL (e.g. "http://localhost:3000").
 * @throws InvalidTunnelOptionsError If options are invalid.
 */
std::string normalize_target_url(const TunnelOptions& options);

} // namespace cloudflared
