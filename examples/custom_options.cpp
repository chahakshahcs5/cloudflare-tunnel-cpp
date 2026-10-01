#include <cloudflared/cloudflared.hpp>
#include <iostream>

int main() {
    try {
        cloudflared::TunnelOptions opts;
        opts.port = static_cast<uint16_t>(5000);
        opts.timeout = std::chrono::seconds(20);
        opts.log_level = cloudflared::LogLevel::Debug; // Verbose output
        opts.extra_args = { "--metrics", "localhost:9090" };

        auto tunnel = cloudflared::create_tunnel(opts);
        std::cout << "URL: " << tunnel->url() << std::endl;

        // Demonstrating idempotent close (calling multiple times is safe)
        tunnel->close();
        tunnel->close();
        std::cout << "Tunnel closed safely." << std::endl;

    } catch (const cloudflared::InvalidTunnelOptionsError& e) {
        std::cerr << "Invalid options: " << e.what() << std::endl;
    } catch (const cloudflared::TunnelTimeoutError& e) {
        std::cerr << "Timeout waiting for tunnel: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
