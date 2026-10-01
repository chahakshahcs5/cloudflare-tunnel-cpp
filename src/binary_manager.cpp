#include "cloudflared/binary_manager.hpp"
#include "cloudflared/cache.hpp"
#include "cloudflared/checksum.hpp"
#include "cloudflared/downloader.hpp"
#include "cloudflared/error.hpp"
#include "cloudflared/platform.hpp"
#include "cloudflared/releases.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace cloudflared {

namespace {

std::mutex g_binary_mutex;

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

/**
 * @brief Tracks and formats binary download progress with speed and ETA calculations.
 */
struct DownloadProgressTracker {
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_log_time;
    size_t last_logged_percent = 0;
    size_t last_logged_bytes = 0;
    bool started = false;

    void update(size_t downloaded, size_t total, const std::function<void(const std::string&)>& log) {
        if (!log) return;
        auto now = std::chrono::steady_clock::now();

        if (!started) {
            started = true;
            start_time = now;
            last_log_time = now;
            last_logged_bytes = downloaded;

            // Initial milestone log
            if (total > 0) {
                size_t percent = (downloaded * 100) / total;
                last_logged_percent = percent;
                char buf[256];
                std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: %zu%% (%.1f MB / %.1f MB)",
                    percent,
                    static_cast<double>(downloaded) / (1024.0 * 1024.0),
                    static_cast<double>(total) / (1024.0 * 1024.0));
                log(buf);
            } else if (downloaded > 0) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: %.1f MB downloaded...",
                    static_cast<double>(downloaded) / (1024.0 * 1024.0));
                log(buf);
            }
            return;
        }

        // Determine if milestone reached
        bool should_log = false;
        size_t percent = 0;
        if (total > 0) {
            percent = (downloaded * 100) / total;
            if (downloaded >= total) {
                should_log = true;
            } else if (percent >= last_logged_percent + 10) {
                should_log = true;
            }
        }

        // Time-based fallback: at least 1.5s since last log and at least 512 KB transferred
        auto elapsed_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_time).count();
        if (!should_log && elapsed_since_last >= 1500 && (downloaded >= last_logged_bytes + 512 * 1024)) {
            should_log = true;
        }

        if (!should_log) return;

        // Calculate speed over elapsed time
        auto total_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        double speed_mb_s = 0.0;
        if (total_elapsed_ms > 200) {
            speed_mb_s = (static_cast<double>(downloaded) / (1024.0 * 1024.0)) / (static_cast<double>(total_elapsed_ms) / 1000.0);
        }

        char buf[256];
        if (total > 0) {
            last_logged_percent = percent;
            double dl_mb = static_cast<double>(downloaded) / (1024.0 * 1024.0);
            double total_mb = static_cast<double>(total) / (1024.0 * 1024.0);

            if (downloaded >= total) {
                if (speed_mb_s >= 0.1) {
                    std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: 100%% (%.1f MB / %.1f MB, %.1f MB/s)",
                        total_mb, total_mb, speed_mb_s);
                } else if (speed_mb_s > 0.0) {
                    double speed_kb_s = (static_cast<double>(downloaded) / 1024.0) / (static_cast<double>(total_elapsed_ms) / 1000.0);
                    std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: 100%% (%.1f MB / %.1f MB, %.0f KB/s)",
                        total_mb, total_mb, speed_kb_s);
                } else {
                    std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: 100%% (%.1f MB / %.1f MB)",
                        total_mb, total_mb);
                }
            } else {
                // Compute ETA
                if (speed_mb_s > 0.001 && downloaded < total) {
                    double remaining_mb = total_mb - dl_mb;
                    int eta_sec = static_cast<int>(remaining_mb / speed_mb_s);
                    std::string speed_str;
                    if (speed_mb_s >= 0.1) {
                        char sbuf[32];
                        std::snprintf(sbuf, sizeof(sbuf), "%.1f MB/s", speed_mb_s);
                        speed_str = sbuf;
                    } else {
                        double speed_kb_s = (static_cast<double>(downloaded) / 1024.0) / (static_cast<double>(total_elapsed_ms) / 1000.0);
                        char sbuf[32];
                        std::snprintf(sbuf, sizeof(sbuf), "%.0f KB/s", speed_kb_s);
                        speed_str = sbuf;
                    }

                    if (eta_sec >= 60) {
                        int eta_min = eta_sec / 60;
                        int rem_sec = eta_sec % 60;
                        std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: %zu%% (%.1f MB / %.1f MB, %s, ETA: %dm %02ds)",
                            percent, dl_mb, total_mb, speed_str.c_str(), eta_min, rem_sec);
                    } else {
                        std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: %zu%% (%.1f MB / %.1f MB, %s, ETA: %ds)",
                            percent, dl_mb, total_mb, speed_str.c_str(), eta_sec);
                    }
                } else {
                    std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: %zu%% (%.1f MB / %.1f MB)",
                        percent, dl_mb, total_mb);
                }
            }
        } else {
            // Total unknown
            double dl_mb = static_cast<double>(downloaded) / (1024.0 * 1024.0);
            if (speed_mb_s >= 0.1) {
                std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: %.1f MB (%.1f MB/s)...", dl_mb, speed_mb_s);
            } else {
                std::snprintf(buf, sizeof(buf), "[cloudflared] Downloading: %.1f MB...", dl_mb);
            }
        }

        last_log_time = now;
        last_logged_bytes = downloaded;
        log(buf);
    }
};

} // namespace

std::filesystem::path ensure_cloudflared(
    const std::optional<std::filesystem::path>& custom_binary_path,
    const std::optional<std::filesystem::path>& custom_cache_dir,
    const std::string& version,
    bool force_download,
    std::function<void(size_t downloaded, size_t total)> progress_callback,
    std::function<void(const std::string&)> log_callback
) {
    auto log = [&log_callback](const std::string& msg) {
        if (log_callback) log_callback(msg);
    };

    // 1. Check custom binary path
    if (custom_binary_path && !custom_binary_path->empty()) {
        if (std::filesystem::exists(*custom_binary_path)) {
            log("[cloudflared] Using custom binary: " + custom_binary_path->string());
            return *custom_binary_path;
        }
        throw BinaryDownloadError("Custom cloudflared binary not found: " + custom_binary_path->string());
    }

    // 2. Check CLOUDFLARED_BIN environment variable
    std::string env_bin = get_env_var("CLOUDFLARED_BIN");
    if (!env_bin.empty() && std::filesystem::exists(env_bin)) {
        log("[cloudflared] Found via CLOUDFLARED_BIN env: " + env_bin);
        return std::filesystem::path(env_bin);
    }

    // Check system PATH
    if (!force_download) {
        const std::vector<std::filesystem::path> common_paths = {
            "/data/data/com.termux/files/usr/bin/cloudflared",
            "/usr/local/bin/cloudflared",
            "/usr/bin/cloudflared",
            "/bin/cloudflared"
        };
        for (const auto& p : common_paths) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
                log("[cloudflared] Found in system PATH: " + p.string());
                return p;
            }
        }
    }

    // 3. Resolve target cache path
    PlatformInfo platform = resolve_current_platform();
    std::filesystem::path target_path = resolve_binary_cache_path(platform, version, custom_cache_dir);
    ReleaseAsset asset = get_release_asset(platform, version);

    log("[cloudflared] Platform: " + platform.os_name + "-" + platform.arch_name + ", version: " + version);
    log("[cloudflared] Cache path: " + target_path.string());

    // Thread-safe binary download lock
    std::lock_guard<std::mutex> lock(g_binary_mutex);

    // Check if valid binary already exists
    if (!force_download && std::filesystem::exists(target_path)) {
        // If checksum is available, verify existing file integrity
        if (!asset.sha256.empty()) {
            log("[cloudflared] Cached binary found, verifying SHA-256...");
            if (verify_file_sha256(target_path, asset.sha256)) {
#if !defined(_WIN32)
                // Enforce read-only executable (0555) for Android 14+ W^X dynamic loading
                chmod(target_path.c_str(), S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
#endif
                log("[cloudflared] Cache hit — checksum verified ✓");
                return target_path; // Cache hit!
            }
            // Checksum mismatch on cached file - delete corrupt file and re-download
            log("[cloudflared] Cache checksum mismatch — re-downloading");
            std::error_code ec;
            std::filesystem::remove(target_path, ec);
        } else {
#if !defined(_WIN32)
            chmod(target_path.c_str(), S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
#endif
            log("[cloudflared] Cache hit (no checksum to verify)");
            return target_path;
        }
    }

    // Prepare directory and temporary file
    std::filesystem::create_directories(target_path.parent_path());
    std::filesystem::path temp_path = get_temp_download_path(target_path);

    // Wire progress callback: automatically log download progress when log_callback is provided
    std::function<void(size_t, size_t)> effective_progress_cb;
    if (progress_callback && !log_callback) {
        effective_progress_cb = progress_callback;
    } else if (!progress_callback && log_callback) {
        auto tracker = std::make_shared<DownloadProgressTracker>();
        effective_progress_cb = [tracker, log](size_t dl, size_t total) {
            tracker->update(dl, total, log);
        };
    } else if (progress_callback && log_callback) {
        auto tracker = std::make_shared<DownloadProgressTracker>();
        effective_progress_cb = [tracker, log, progress_callback](size_t dl, size_t total) {
            progress_callback(dl, total);
            tracker->update(dl, total, log);
        };
    }

    try {
        // Download directly to temporary path
        log("[cloudflared] Downloading from: " + asset.download_url);
        download_file(asset.download_url, temp_path, effective_progress_cb);

        auto file_size = std::filesystem::file_size(temp_path);
        log("[cloudflared] Download complete (" + std::to_string(file_size / 1024) + " KB)");

        // Mandatory SHA256 checksum verification
        if (!asset.sha256.empty()) {
            log("[cloudflared] Verifying SHA-256 checksum...");
            std::string actual_sha256 = compute_file_sha256_hex(temp_path);
            if (!verify_file_sha256(temp_path, asset.sha256)) {
                std::error_code ec;
                std::filesystem::remove(temp_path, ec);
                log("[cloudflared] SHA-256 verification FAILED — expected: " + asset.sha256 + ", got: " + actual_sha256);
                throw BinaryVerificationError(asset.sha256, actual_sha256);
            }
            log("[cloudflared] SHA-256 verified ✓");
        }

        // Extract macOS tarball if needed
        if (platform.os == OS::MacOS && asset.filename.ends_with(".tgz")) {
            log("[cloudflared] Extracting macOS tarball...");
            std::string cmd = "tar -xzf \"" + temp_path.string() + "\" -C \"" + target_path.parent_path().string() + "\"";
            int res = std::system(cmd.c_str());
            std::error_code ec;
            std::filesystem::remove(temp_path, ec);
            if (res != 0) {
                throw BinaryDownloadError("Failed to extract cloudflared tarball on macOS");
            }
        } else {
            // Atomic rename to final binary path
            std::error_code ec;
            std::filesystem::rename(temp_path, target_path, ec);
            if (ec) {
                // If rename failed (e.g. across mount boundaries), copy and remove
                std::filesystem::copy_file(temp_path, target_path, std::filesystem::copy_options::overwrite_existing);
                std::filesystem::remove(temp_path, ec);
            }
        }

#if !defined(_WIN32)
        // Set read-only executable permissions (chmod 0555).
        // CRITICAL FOR ANDROID 14+ (API 34+): dlopen() rejects any file with write permissions!
        chmod(target_path.c_str(), S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        log("[cloudflared] Permissions set (chmod 0555)");
#endif

        log("[cloudflared] Binary installed successfully: " + target_path.string());
        return target_path;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        throw;
    }
}

bool remove_cached_cloudflared(
    const std::optional<std::filesystem::path>& custom_cache_dir,
    const std::string& version
) {
    std::lock_guard<std::mutex> lock(g_binary_mutex);
    PlatformInfo platform = resolve_current_platform();
    std::filesystem::path target_path = resolve_binary_cache_path(platform, version, custom_cache_dir);

    std::error_code ec;
    return std::filesystem::remove(target_path, ec);
}

} // namespace cloudflared
