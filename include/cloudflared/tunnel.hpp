#pragma once

#include "cloudflared/types.hpp"
#include <memory>
#include <optional>
#include <string>

namespace cloudflared {

/**
 * @brief Represents an active Cloudflare tunnel instance.
 */
class Tunnel {
public:
    virtual ~Tunnel() = default;

    /**
     * @brief The public HTTPS URL assigned to this tunnel by Cloudflare
     * (e.g. "https://random-name.trycloudflare.com").
     */
    virtual const std::string& url() const = 0;

    /**
     * @brief Process ID of the running cloudflared child process.
     */
    virtual std::optional<int64_t> pid() const = 0;

    /**
     * @brief Gracefully terminates the tunnel and cloudflared process.
     * Safe to call multiple times (idempotent).
     */
    virtual void close() = 0;

    /**
     * @brief Checks if the tunnel has been closed.
     */
    virtual bool is_closed() const = 0;

    /**
     * @brief Blocks until the underlying tunnel process exits.
     */
    virtual void wait() = 0;
};

/**
 * @brief Primary library entry point: creates and exposes a local service
 * via Cloudflare Tunnel.
 *
 * Example:
 * @code
 * auto tunnel = cloudflared::create_tunnel({ .port = 3000 });
 * std::cout << "Public URL: " << tunnel->url() << std::endl;
 * @endcode
 *
 * @param options Tunnel options (port or target url, timeouts, logging).
 * @return std::shared_ptr<Tunnel> Established tunnel instance.
 * @throws InvalidTunnelOptionsError If invalid port or options provided.
 * @throws TunnelTimeoutError If public URL is not detected within timeout.
 * @throws CloudflaredStartupError If cloudflared fails to launch.
 */
std::shared_ptr<Tunnel> create_tunnel(const TunnelOptions& options);

} // namespace cloudflared
