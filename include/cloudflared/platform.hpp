#pragma once

#include <string>

namespace cloudflared {

enum class OS {
    Windows,
    Linux,
    MacOS,
    Android,
    Unknown
};

enum class Arch {
    X64,
    Arm64,
    Arm,
    X86,
    Unknown
};

struct PlatformInfo {
    OS os = OS::Unknown;
    Arch arch = Arch::Unknown;
    std::string os_name;        ///< "win32", "linux", "darwin"
    std::string arch_name;      ///< "x64", "arm64", "arm", "386"
    std::string binary_name;    ///< "cloudflared.exe" on Windows, "cloudflared" on Unix
    std::string asset_filename; ///< Official release asset name (e.g. "cloudflared-windows-amd64.exe")
};

/**
 * @brief Resolves platform and architecture for the current build/host.
 * @throws UnsupportedPlatformError if OS or architecture is not supported.
 */
PlatformInfo resolve_current_platform();

/**
 * @brief Resolves platform information for a specific OS and Arch.
 * @throws UnsupportedPlatformError if combination is not supported.
 */
PlatformInfo resolve_platform(OS os, Arch arch);

std::string to_string(OS os);
std::string to_string(Arch arch);

} // namespace cloudflared
