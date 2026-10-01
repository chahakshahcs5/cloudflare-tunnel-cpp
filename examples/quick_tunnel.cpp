#include <cloudflared/cloudflared.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    std::cout << "Creating quick tunnel for port " << port << "..." << std::endl;

    try {
        // Zero-config: automatically resolves platform, verifies SHA256, downloads & starts tunnel
        auto tunnel = cloudflared::create_tunnel({
            .port = port,
            .log_level = cloudflared::LogLevel::Info
        });

        std::cout << "\n============================================\n";
        std::cout << "Tunnel is live!\n";
        std::cout << "Public HTTPS URL: " << tunnel->url() << "\n";
        if (auto pid = tunnel->pid()) {
            std::cout << "Process PID:      " << *pid << "\n";
        }
        std::cout << "============================================\n\n";

        std::cout << "Running tunnel for 10 seconds before clean shutdown..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));

        std::cout << "Shutting down tunnel..." << std::endl;
        tunnel->close();
        std::cout << "Tunnel successfully closed." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Tunnel failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
