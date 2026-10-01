#include <cloudflared/cloudflared.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    try {
        // Expose an explicit local HTTP service URL
        auto tunnel = cloudflared::create_tunnel({
            .url = "http://localhost:3000",
            .log_level = cloudflared::LogLevel::Info
        });

        std::cout << "Tunnel online at: " << tunnel->url() << std::endl;

        // Clean close
        std::this_thread::sleep_for(std::chrono::seconds(5));
        tunnel->close();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
