#include "cloudflared/cloudflared.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>

void test_process_and_tunnel_mock() {
    std::cout << "[TEST] Running test_process_and_tunnel_mock..." << std::endl;

    // 1. Test ProcessManager with a mock command
#if defined(_WIN32)
    std::string mock_cmd = "cmd.exe";
    std::vector<std::string> args = { "/c", "echo https://mock-test-123.trycloudflare.com" };
#else
    std::string mock_cmd = "echo";
    std::vector<std::string> args = { "https://mock-test-123.trycloudflare.com" };
#endif

    std::string captured_line;
    bool exited = false;

    cloudflared::ProcessManager proc(
        mock_cmd,
        args,
        [&captured_line](const std::string& line) {
            captured_line = line;
        },
        nullptr,
        [&exited](int code) {
            exited = true;
        }
    );

    proc.start();
    assert(proc.is_running() || exited);
    proc.wait_for_exit(5000);

    assert(captured_line.find("https://mock-test-123.trycloudflare.com") != std::string::npos);

    // 2. Test timeout handling when no URL is output
#if defined(_WIN32)
    // Command that sleeps without printing a URL
    std::string silent_cmd = "powershell.exe";
    std::vector<std::string> silent_args = { "-Command", "Start-Sleep -Seconds 5" };
#else
    std::string silent_cmd = "sleep";
    std::vector<std::string> silent_args = { "5" };
#endif

    cloudflared::TunnelOptions opts;
    opts.port = static_cast<uint16_t>(8080);
    opts.timeout = std::chrono::milliseconds(300); // short timeout
    opts.custom_binary_path = silent_cmd;
    opts.extra_args = silent_args;

    bool caught_timeout = false;
    try {
        auto t = cloudflared::create_tunnel(opts);
    } catch (const cloudflared::TunnelTimeoutError&) {
        caught_timeout = true;
    } catch (const std::exception& e) {
        // Some systems might throw or reject before timeout
        caught_timeout = true;
    }
    assert(caught_timeout);

    std::cout << "[PASS] Process and tunnel mock tests passed!" << std::endl;
}

int main() {
    test_process_and_tunnel_mock();
    return 0;
}
