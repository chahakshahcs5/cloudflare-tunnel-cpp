#include "cloudflared/platform.hpp"
#include "cloudflared/error.hpp"
#include <cassert>
#include <iostream>

void test_platform_resolution() {
    std::cout << "[TEST] Running test_platform_resolution..." << std::endl;

    // Windows x64
    auto win64 = cloudflared::resolve_platform(cloudflared::OS::Windows, cloudflared::Arch::X64);
    assert(win64.os_name == "win32");
    assert(win64.arch_name == "x64");
    assert(win64.binary_name == "cloudflared.exe");
    assert(win64.asset_filename == "cloudflared-windows-amd64.exe");

    // Windows 386
    auto win32 = cloudflared::resolve_platform(cloudflared::OS::Windows, cloudflared::Arch::X86);
    assert(win32.asset_filename == "cloudflared-windows-386.exe");

    // Linux x64
    auto lin64 = cloudflared::resolve_platform(cloudflared::OS::Linux, cloudflared::Arch::X64);
    assert(lin64.os_name == "linux");
    assert(lin64.arch_name == "x64");
    assert(lin64.binary_name == "cloudflared");
    assert(lin64.asset_filename == "cloudflared-linux-amd64");

    // Linux arm64
    auto lin_arm64 = cloudflared::resolve_platform(cloudflared::OS::Linux, cloudflared::Arch::Arm64);
    assert(lin_arm64.asset_filename == "cloudflared-linux-arm64");

    // macOS x64 & arm64
    auto mac64 = cloudflared::resolve_platform(cloudflared::OS::MacOS, cloudflared::Arch::X64);
    assert(mac64.os_name == "darwin");
    assert(mac64.asset_filename == "cloudflared-darwin-amd64.tgz");

    auto mac_arm64 = cloudflared::resolve_platform(cloudflared::OS::MacOS, cloudflared::Arch::Arm64);
    assert(mac_arm64.asset_filename == "cloudflared-darwin-arm64.tgz");

    // Unsupported combinations
    bool caught = false;
    try {
        cloudflared::resolve_platform(cloudflared::OS::Windows, cloudflared::Arch::Arm);
    } catch (const cloudflared::UnsupportedPlatformError&) {
        caught = true;
    }
    assert(caught);

    caught = false;
    try {
        cloudflared::resolve_platform(cloudflared::OS::Unknown, cloudflared::Arch::X64);
    } catch (const cloudflared::UnsupportedPlatformError&) {
        caught = true;
    }
    assert(caught);

    // Host platform
    auto current = cloudflared::resolve_current_platform();
    assert(!current.os_name.empty());
    assert(!current.arch_name.empty());
    assert(!current.binary_name.empty());

    std::cout << "[PASS] Platform resolution tests passed!" << std::endl;
}

int main() {
    test_platform_resolution();
    return 0;
}
