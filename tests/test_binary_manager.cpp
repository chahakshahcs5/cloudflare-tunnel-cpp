#include "cloudflared/binary_manager.hpp"
#include "cloudflared/cache.hpp"
#include "cloudflared/platform.hpp"
#include "cloudflared/error.hpp"
#include "cloudflared/downloader.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

void test_cache_and_binary_manager() {
    std::cout << "[TEST] Running test_cache_and_binary_manager..." << std::endl;

    // 1. Cache directory resolution
    auto cache_dir = cloudflared::get_default_cache_directory();
    assert(!cache_dir.empty());
    assert(cache_dir.string().find("cloudflared-cpp") != std::string::npos);

    // 2. Binary path resolution
    cloudflared::PlatformInfo win_info;
    win_info.os = cloudflared::OS::Windows;
    win_info.arch = cloudflared::Arch::X64;
    win_info.os_name = "win32";
    win_info.arch_name = "x64";
    win_info.binary_name = "cloudflared.exe";

    auto bin_path = cloudflared::resolve_binary_cache_path(win_info, "2026.8.3");
    assert(bin_path.string().find("2026.8.3") != std::string::npos);
    assert(bin_path.string().find("win32-x64") != std::string::npos);
    assert(bin_path.filename().string() == "cloudflared.exe");

    // 3. Custom binary path check
    std::filesystem::path temp_fake_bin = std::filesystem::temp_directory_path() / "mock_cloudflared.exe";
    {
        std::ofstream f(temp_fake_bin);
        f << "mock binary content";
    }

    auto resolved = cloudflared::ensure_cloudflared(temp_fake_bin);
    assert(resolved == temp_fake_bin);

    std::error_code ec;
    std::filesystem::remove(temp_fake_bin, ec);

    // 4. Non-existent custom binary path throws error
    bool caught_missing = false;
    try {
        cloudflared::ensure_cloudflared(std::filesystem::path("C:/does_not_exist_xyz/cloudflared.exe"));
    } catch (const cloudflared::BinaryDownloadError&) {
        caught_missing = true;
    }
    std::cout << "[PASS] Binary manager cache tests passed!" << std::endl;
}

void test_binary_download_progress_logging() {
    std::cout << "[TEST] Running test_binary_download_progress_logging..." << std::endl;

    std::vector<std::string> logged_messages;
    auto mock_logger = [&logged_messages](const std::string& msg) {
        logged_messages.push_back(msg);
    };

    // Register a mock downloader that simulates streamed chunks with progress updates
    cloudflared::set_custom_downloader(
        [](const std::string& /*url*/, const std::filesystem::path& dest, std::function<void(size_t, size_t)> progress_cb) -> bool {
            std::filesystem::create_directories(dest.parent_path());
            std::ofstream f(dest, std::ios::binary);
            f << "mock cloudflared content";
            f.close();

            if (progress_cb) {
                // Simulate progressive chunk reports
                progress_cb(10 * 1024 * 1024, 70 * 1024 * 1024);
                progress_cb(35 * 1024 * 1024, 70 * 1024 * 1024);
                progress_cb(70 * 1024 * 1024, 70 * 1024 * 1024);
            }
            return true;
        }
    );

    std::filesystem::path temp_cache = std::filesystem::temp_directory_path() / "test_cloudflared_cache";
    std::filesystem::remove_all(temp_cache);

    try {
        cloudflared::ensure_cloudflared(
            std::nullopt,
            temp_cache,
            cloudflared::PINNED_CLOUDFLARED_VERSION,
            true, // force download
            nullptr, // let ensure_cloudflared activate DownloadProgressTracker
            mock_logger
        );
    } catch (const cloudflared::BinaryVerificationError&) {
        // Expected since mock file does not match pinned official SHA256
    }

    // Reset custom downloader
    cloudflared::set_custom_downloader(nullptr);
    std::filesystem::remove_all(temp_cache);

    // Verify that progress messages were generated and formatted properly
    bool found_start_or_percent = false;
    bool found_complete = false;
    for (const auto& line : logged_messages) {
        if (line.find("[cloudflared] Downloading:") != std::string::npos) {
            found_start_or_percent = true;
        }
        if (line.find("100%") != std::string::npos && line.find("70.0 MB / 70.0 MB") != std::string::npos) {
            found_complete = true;
        }
    }

    assert(found_start_or_percent);
    assert(found_complete);

    std::cout << "[PASS] Binary download progress logging tests passed!" << std::endl;
}

int main() {
    test_cache_and_binary_manager();
    test_binary_download_progress_logging();
    return 0;
}
