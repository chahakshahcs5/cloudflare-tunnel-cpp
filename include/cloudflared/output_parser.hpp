#pragma once

#include "cloudflared/types.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace cloudflared {

/**
 * @brief Parses cloudflared log output lines to extract URLs and status events.
 */
class OutputParser {
public:
    /**
     * @brief Searches for a trycloudflare public URL in the given output text.
     * @return std::optional<std::string> The full URL if found (e.g. "https://foo-bar.trycloudflare.com").
     */
    static std::optional<std::string> extract_tunnel_url(std::string_view text);

    /**
     * @brief Searches for an active edge connection establishment log line.
     */
    static std::optional<Connection> extract_connection(std::string_view text);

    /**
     * @brief Searches for a connection termination or unregistration event.
     * @return std::optional<int> Connection index (connIndex) if found.
     */
    static std::optional<int> extract_disconnection_index(std::string_view text);

    /**
     * @brief Searches for the metrics server URL in log output.
     */
    static std::optional<std::string> extract_metrics_url(std::string_view text);
};

} // namespace cloudflared
