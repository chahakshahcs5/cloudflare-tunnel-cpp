#include "cloudflared/types.hpp"
#include "cloudflared/error.hpp"
#include <cassert>
#include <iostream>

void test_options_normalization() {
    std::cout << "[TEST] Running test_options_normalization..." << std::endl;

    // Normalizing port
    cloudflared::TunnelOptions opt1;
    opt1.port = 3000;
    assert(cloudflared::normalize_target_url(opt1) == "http://localhost:3000");

    cloudflared::TunnelOptions opt2;
    opt2.port = 80;
    assert(cloudflared::normalize_target_url(opt2) == "http://localhost:80");

    // Normalizing explicit URL
    cloudflared::TunnelOptions opt3;
    opt3.url = "http://localhost:8080";
    assert(cloudflared::normalize_target_url(opt3) == "http://localhost:8080");

    cloudflared::TunnelOptions opt4;
    opt4.url = "127.0.0.1:4000";
    assert(cloudflared::normalize_target_url(opt4) == "http://127.0.0.1:4000");

    // Port 0 rejection
    cloudflared::TunnelOptions opt_zero;
    opt_zero.port = 0;
    bool caught_zero = false;
    try {
        cloudflared::normalize_target_url(opt_zero);
    } catch (const cloudflared::InvalidTunnelOptionsError&) {
        caught_zero = true;
    }
    assert(caught_zero);

    // Empty options rejection
    cloudflared::TunnelOptions opt_empty;
    bool caught_empty = false;
    try {
        cloudflared::normalize_target_url(opt_empty);
    } catch (const cloudflared::InvalidTunnelOptionsError&) {
        caught_empty = true;
    }
    assert(caught_empty);

    std::cout << "[PASS] Options normalization tests passed!" << std::endl;
}

int main() {
    test_options_normalization();
    return 0;
}
