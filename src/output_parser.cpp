#include "cloudflared/output_parser.hpp"
#include <regex>

namespace cloudflared {

namespace {

const std::regex REGEX_URL(R"(https:\/\/([a-zA-Z0-9-]+)\.trycloudflare\.com)");
const std::regex REGEX_CONN(R"(connection[= ]([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-4[0-9a-fA-F]{3}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}))");
const std::regex REGEX_IP(R"(ip=([0-9.]+))");
const std::regex REGEX_LOCATION(R"(location=([A-Za-z0-9]+))");
const std::regex REGEX_INDEX(R"(connIndex=(\d+))");
const std::regex REGEX_DISCONNECT(R"((?:Unregistered tunnel connection|Connection terminated).*connIndex=(\d+))");
const std::regex REGEX_METRICS(R"(metrics server on ([0-9.:]+\/metrics))");

} // namespace

std::optional<std::string> OutputParser::extract_tunnel_url(std::string_view text) {
    std::string s(text);
    auto words_begin = std::sregex_iterator(s.begin(), s.end(), REGEX_URL);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch m = *i;
        std::string sub = m[1].str();
        if (sub != "api" && sub != "www") {
            return m.str(0);
        }
    }
    return std::nullopt;
}

std::optional<Connection> OutputParser::extract_connection(std::string_view text) {
    std::string s(text);
    std::smatch conn_match, ip_match, loc_match, idx_match;

    if (std::regex_search(s, conn_match, REGEX_CONN) &&
        std::regex_search(s, ip_match, REGEX_IP) &&
        std::regex_search(s, loc_match, REGEX_LOCATION) &&
        std::regex_search(s, idx_match, REGEX_INDEX)) {

        Connection conn;
        conn.id = conn_match[1].str();
        conn.ip = ip_match[1].str();
        conn.location = loc_match[1].str();
        conn.index = std::stoi(idx_match[1].str());
        return conn;
    }

    return std::nullopt;
}

std::optional<int> OutputParser::extract_disconnection_index(std::string_view text) {
    std::string s(text);
    std::smatch match;
    if (std::regex_search(s, match, REGEX_DISCONNECT)) {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}

std::optional<std::string> OutputParser::extract_metrics_url(std::string_view text) {
    std::string s(text);
    std::smatch match;
    if (std::regex_search(s, match, REGEX_METRICS)) {
        return match[1].str();
    }
    return std::nullopt;
}

} // namespace cloudflared
