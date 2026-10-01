#include "cloudflared/cache.hpp"
#include <cstdlib>
#include <chrono>
#include <atomic>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#define GETPID() _getpid()
#else
#include <unistd.h>
#define GETPID() getpid()
#endif

namespace cloudflared {

namespace {

std::string get_env_var(const char* name) {
#if defined(_WIN32)
    char* val = nullptr;
    size_t len = 0;
    if (_dupenv_s(&val, &len, name) == 0 && val != nullptr) {
        std::string res(val);
        free(val);
        return res;
    }
    return "";
#else
    const char* val = std::getenv(name);
    return val ? std::string(val) : "";
#endif
}

std::atomic<uint64_t> g_temp_counter{0};

} // namespace

std::filesystem::path get_default_cache_directory() {
    std::string env_override = get_env_var("CLOUDFLARED_CACHE_DIR");
    if (!env_override.empty()) {
        return std::filesystem::path(env_override);
    }

#if defined(_WIN32)
    std::string local_app_data = get_env_var("LOCALAPPDATA");
    if (!local_app_data.empty()) {
        return std::filesystem::path(local_app_data) / "cloudflared-cpp";
    }

    std::string app_data = get_env_var("APPDATA");
    if (!app_data.empty()) {
        return std::filesystem::path(app_data) / "cloudflared-cpp";
    }

    std::string user_profile = get_env_var("USERPROFILE");
    if (!user_profile.empty()) {
        return std::filesystem::path(user_profile) / ".cache" / "cloudflared-cpp";
    }

    return std::filesystem::temp_directory_path() / "cloudflared-cpp";
#elif defined(__APPLE__)
    std::string home = get_env_var("HOME");
    if (!home.empty()) {
        return std::filesystem::path(home) / "Library" / "Caches" / "cloudflared-cpp";
    }
    return std::filesystem::temp_directory_path() / "cloudflared-cpp";
#else
    std::string xdg_cache = get_env_var("XDG_CACHE_HOME");
    if (!xdg_cache.empty()) {
        return std::filesystem::path(xdg_cache) / "cloudflared-cpp";
    }

#if defined(__ANDROID__)
    std::string home = get_env_var("HOME");
    if (!home.empty() && home != "/" && home != "/root") {
        return std::filesystem::path(home) / ".cache" / "cloudflared-cpp";
    }
    return "/data/local/tmp/cloudflared-cpp";
#else
    std::string home = get_env_var("HOME");
    if (!home.empty() && home != "/") {
        return std::filesystem::path(home) / ".cache" / "cloudflared-cpp";
    }

    std::error_code ec;
    auto tmp = std::filesystem::temp_directory_path(ec);
    if (!ec && !tmp.empty() && tmp != "/") {
        return tmp / "cloudflared-cpp";
    }
    return "/data/local/tmp/cloudflared-cpp";
#endif
#endif
}

std::filesystem::path resolve_binary_cache_path(
    const PlatformInfo& platform,
    const std::string& version,
    const std::optional<std::filesystem::path>& custom_cache_dir
) {
    std::filesystem::path base_cache = custom_cache_dir ? *custom_cache_dir : get_default_cache_directory();
    std::string platform_tag = platform.os_name + "-" + platform.arch_name;
    return base_cache / "cloudflared" / version / platform_tag / platform.binary_name;
}

std::filesystem::path get_temp_download_path(const std::filesystem::path& target_binary_path) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t counter = g_temp_counter.fetch_add(1);
    std::string suffix = ".tmp." + std::to_string(GETPID()) + "." + std::to_string(now) + "." + std::to_string(counter);
    return target_binary_path.parent_path() / (target_binary_path.filename().string() + suffix);
}

} // namespace cloudflared
