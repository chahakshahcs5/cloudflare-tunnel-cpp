#include "cloudflared/output_parser.hpp"
#include <cassert>
#include <iostream>

void test_output_parser() {
    std::cout << "[TEST] Running test_output_parser..." << std::endl;

    // 1. URL extraction from typical cloudflared banner
    std::string banner_line = "2026-09-07T08:00:00Z INF |  https://happy-cat-1234.trycloudflare.com                           |";
    auto url_opt = cloudflared::OutputParser::extract_tunnel_url(banner_line);
    assert(url_opt.has_value());
    assert(*url_opt == "https://happy-cat-1234.trycloudflare.com");

    // 2. URL extraction from plain string
    assert(cloudflared::OutputParser::extract_tunnel_url("Visit https://test-tunnel-xyz.trycloudflare.com now").value()
           == "https://test-tunnel-xyz.trycloudflare.com");

    // Negative case
    assert(!cloudflared::OutputParser::extract_tunnel_url("No url in this line").has_value());

    // 3. Connection parsing
    std::string conn_line = "2026-09-07T08:01:00Z INF Registered tunnel connection connIndex=0 connection=4db5ec6e-4076-45c5-8752-745071bc2567 event=0 ip=198.41.200.193 location=tpe01 protocol=quic";
    auto conn_opt = cloudflared::OutputParser::extract_connection(conn_line);
    assert(conn_opt.has_value());
    assert(conn_opt->id == "4db5ec6e-4076-45c5-8752-745071bc2567");
    assert(conn_opt->ip == "198.41.200.193");
    assert(conn_opt->location == "tpe01");
    assert(conn_opt->index == 0);

    // 4. Disconnection parsing
    std::string disc_line = "2026-09-07T08:02:00Z INF Connection terminated error=\"connection with edge closed\" connIndex=1";
    auto disc_opt = cloudflared::OutputParser::extract_disconnection_index(disc_line);
    assert(disc_opt.has_value());
    assert(*disc_opt == 1);

    // 5. Metrics parsing
    std::string metrics_line = "2026-09-07T08:00:01Z INF Starting metrics server on 127.0.0.1:2020/metrics";
    auto metrics_opt = cloudflared::OutputParser::extract_metrics_url(metrics_line);
    assert(metrics_opt.has_value());
    assert(*metrics_opt == "127.0.0.1:2020/metrics");

    std::cout << "[PASS] Output parser tests passed!" << std::endl;
}

int main() {
    test_output_parser();
    return 0;
}
