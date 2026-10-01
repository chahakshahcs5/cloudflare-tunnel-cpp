#include "cloudflared/platform.hpp"
#include "cloudflared/error.hpp"

namespace cloudflared {

namespace {

#if defined(_WIN32) || defined(_WIN64)
constexpr OS HOST_OS = OS::Windows;
#elif defined(__ANDROID__)
constexpr OS HOST_OS = OS::Android;
#elif defined(__APPLE__) && defined(__MACH__)
constexpr OS HOST_OS = OS::MacOS;
#elif defined(__linux__)
constexpr OS HOST_OS = OS::Linux;
#else
constexpr OS HOST_OS = OS::Unknown;
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
constexpr Arch HOST_ARCH = Arch::X64;
#elif defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
constexpr Arch HOST_ARCH = Arch::Arm64;
#elif defined(_M_ARM) || defined(__arm__)
constexpr Arch HOST_ARCH = Arch::Arm;
#elif defined(_M_IX86) || defined(__i386__)
constexpr Arch HOST_ARCH = Arch::X86;
#else
constexpr Arch HOST_ARCH = Arch::Unknown;
#endif

} // namespace

std::string to_string(OS os) {
    switch (os) {
        case OS::Windows: return "windows";
        case OS::Linux:   return "linux";
        case OS::MacOS:   return "darwin";
        case OS::Android: return "android";
        case OS::Unknown: return "unknown";
    }
    return "unknown";
}

std::string to_string(Arch arch) {
    switch (arch) {
        case Arch::X64:   return "amd64";
        case Arch::Arm64: return "arm64";
        case Arch::Arm:   return "arm";
        case Arch::X86:   return "386";
        case Arch::Unknown: return "unknown";
    }
    return "unknown";
}

PlatformInfo resolve_platform(OS os, Arch arch) {
    PlatformInfo info;
    info.os = os;
    info.arch = arch;
    info.os_name = to_string(os);
    info.arch_name = to_string(arch);

    if (os == OS::Windows) {
        info.binary_name = "cloudflared.dll";
        if (arch == Arch::X64) {
            info.asset_filename = "cloudflared-windows-amd64.dll";
            return info;
        } else if (arch == Arch::X86) {
            info.asset_filename = "cloudflared-windows-386.dll";
            return info;
        }
    } else if (os == OS::Linux) {
        info.binary_name = "libcloudflared.so";
        if (arch == Arch::X64) {
            info.asset_filename = "cloudflared-linux-amd64.so";
            return info;
        } else if (arch == Arch::Arm64) {
            info.asset_filename = "cloudflared-linux-arm64.so";
            return info;
        } else if (arch == Arch::Arm) {
            info.asset_filename = "cloudflared-linux-arm.so";
            return info;
        } else if (arch == Arch::X86) {
            info.asset_filename = "cloudflared-linux-386.so";
            return info;
        }
    } else if (os == OS::Android) {
        info.binary_name = "libcloudflared.so";
        if (arch == Arch::Arm64) {
            info.asset_filename = "cloudflared-android-arm64.so";
            return info;
        } else if (arch == Arch::Arm) {
            info.asset_filename = "cloudflared-android-arm.so";
            return info;
        } else if (arch == Arch::X64) {
            info.asset_filename = "cloudflared-android-amd64.so";
            return info;
        } else if (arch == Arch::X86) {
            info.asset_filename = "cloudflared-android-386.so";
            return info;
        }
    } else if (os == OS::MacOS) {
        info.binary_name = "libcloudflared.dylib";
        if (arch == Arch::X64) {
            info.asset_filename = "cloudflared-darwin-amd64.dylib";
            return info;
        } else if (arch == Arch::Arm64) {
            info.asset_filename = "cloudflared-darwin-arm64.dylib";
            return info;
        }
    }

    throw UnsupportedPlatformError(info.os_name + " (" + info.arch_name + ")");
}

PlatformInfo resolve_current_platform() {
    return resolve_platform(HOST_OS, HOST_ARCH);
}

} // namespace cloudflared
