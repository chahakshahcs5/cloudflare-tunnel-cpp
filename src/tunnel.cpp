#include "cloudflared/tunnel.hpp"
#include "cloudflared/binary_manager.hpp"
#include "cloudflared/error.hpp"
#include "cloudflared/output_parser.hpp"
#include "cloudflared/process.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
using LibHandle = HMODULE;
#else
#include <dlfcn.h>
#include <unistd.h>
using LibHandle = void*;
#endif

namespace cloudflared {

std::string normalize_target_url(const TunnelOptions& options) {
    if (options.token && !options.token->empty() && !options.port && (!options.url || options.url->empty())) {
        return "";
    }

    if (options.port) {
        uint16_t p = *options.port;
        if (p == 0) {
            throw InvalidTunnelOptionsError("Port number must be between 1 and 65535 (got 0)");
        }
        return "http://localhost:" + std::to_string(p);
    }

    if (options.url && !options.url->empty()) {
        const std::string& u = *options.url;
        if (u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0 || u.rfind("tcp://", 0) == 0) {
            return u;
        }
        // If scheme missing, default to http://
        return "http://" + u;
    }

    if (options.token && !options.token->empty()) {
        return "";
    }

    throw InvalidTunnelOptionsError("Either 'port', 'url', or 'token' must be specified in TunnelOptions");
}

namespace {

using FnSetSilentMode = void (*)(int);
using FnInit = int (*)(void);
using FnRun = int (*)(char*);
using FnStop = int (*)(void);
using FnFreeString = void (*)(char*);
using FnVersion = char* (*)(void);
using FnGetTunnelURL = char* (*)(void);
using FnGetTunnelStatus = int (*)(void);
using FnStartQuickTunnel = int (*)(int);

struct CloudflaredLibApi {
    LibHandle handle{nullptr};
    FnSetSilentMode set_silent_mode{nullptr};
    FnInit init{nullptr};
    FnRun run{nullptr};
    FnStop stop{nullptr};
    FnFreeString free_string{nullptr};
    FnVersion version{nullptr};
    FnGetTunnelURL get_tunnel_url{nullptr};
    FnGetTunnelStatus get_tunnel_status{nullptr};
    FnStartQuickTunnel start_quick_tunnel{nullptr};

    static std::shared_ptr<CloudflaredLibApi> load(const std::filesystem::path& path) {
        auto api = std::make_shared<CloudflaredLibApi>();
#if defined(_WIN32)
        api->handle = LoadLibraryW(path.c_str());
        if (!api->handle) {
            DWORD err = GetLastError();
            throw CloudflaredStartupError("Failed to LoadLibraryW(" + path.string() + "): code " + std::to_string(err));
        }
        auto get_sym = [h = api->handle](const char* sym) -> FARPROC {
            return GetProcAddress(h, sym);
        };
#else
        api->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!api->handle) {
            const char* err = dlerror();
            throw CloudflaredStartupError("Failed to dlopen(" + path.string() + "): " + (err ? err : "unknown error"));
        }
        auto get_sym = [h = api->handle](const char* sym) -> void* {
            return dlsym(h, sym);
        };
#endif

        api->set_silent_mode = reinterpret_cast<FnSetSilentMode>(get_sym("CloudflaredSetSilentMode"));
        api->init = reinterpret_cast<FnInit>(get_sym("CloudflaredInit"));
        api->run = reinterpret_cast<FnRun>(get_sym("CloudflaredRun"));
        api->stop = reinterpret_cast<FnStop>(get_sym("CloudflaredStop"));
        api->free_string = reinterpret_cast<FnFreeString>(get_sym("CloudflaredFreeString"));
        api->version = reinterpret_cast<FnVersion>(get_sym("CloudflaredVersion"));
        api->get_tunnel_url = reinterpret_cast<FnGetTunnelURL>(get_sym("CloudflaredGetTunnelURL"));
        api->get_tunnel_status = reinterpret_cast<FnGetTunnelStatus>(get_sym("CloudflaredGetTunnelStatus"));
        api->start_quick_tunnel = reinterpret_cast<FnStartQuickTunnel>(get_sym("CloudflaredStartQuickTunnel"));

        if (!api->init || !api->stop || !api->get_tunnel_url || !api->start_quick_tunnel) {
            throw CloudflaredStartupError("Shared library " + path.string() + " is missing required Cloudflared API symbols");
        }
        return api;
    }

    ~CloudflaredLibApi() {
        // Retain library loaded in process: Go c-shared runtime threads & signal handlers
        // remain initialized across the lifetime of the process.
    }
};

class TunnelSharedLibImpl : public Tunnel {
public:
    TunnelSharedLibImpl(
        std::string target_url,
        std::string public_url,
        std::shared_ptr<CloudflaredLibApi> api,
        LogLevel log_level
    )
        : m_target_url(std::move(target_url))
        , m_public_url(std::move(public_url))
        , m_api(std::move(api))
        , m_log_level(log_level)
    {}

    ~TunnelSharedLibImpl() override {
        close();
    }

    const std::string& url() const override {
        return m_public_url;
    }

    std::optional<int64_t> pid() const override {
#if defined(_WIN32)
        return static_cast<int64_t>(GetCurrentProcessId());
#else
        return static_cast<int64_t>(getpid());
#endif
    }

    void close() override {
        bool expected = false;
        if (m_closed.compare_exchange_strong(expected, true)) {
            if (m_log_level >= LogLevel::Info) {
                std::cout << "[cloudflared] Closing in-process tunnel: " << m_public_url << std::endl;
            }
            if (m_api && m_api->stop) {
                m_api->stop();
            }
        }
    }

    bool is_closed() const override {
        return m_closed.load();
    }

    void wait() override {
        while (!m_closed.load()) {
            if (m_api && m_api->get_tunnel_status) {
                int st = m_api->get_tunnel_status();
                if (st == 0 || st == 3) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

private:
    std::string m_target_url;
    std::string m_public_url;
    std::shared_ptr<CloudflaredLibApi> m_api;
    LogLevel m_log_level;
    std::atomic<bool> m_closed{false};
};

class TunnelProcessImpl : public Tunnel {
public:
    TunnelProcessImpl(
        std::string target_url,
        std::string public_url,
        std::unique_ptr<ProcessManager> process,
        LogLevel log_level
    )
        : m_target_url(std::move(target_url))
        , m_public_url(std::move(public_url))
        , m_process(std::move(process))
        , m_log_level(log_level)
    {}

    ~TunnelProcessImpl() override {
        close();
    }

    const std::string& url() const override {
        return m_public_url;
    }

    std::optional<int64_t> pid() const override {
        return m_process ? m_process->pid() : std::nullopt;
    }

    void close() override {
        bool expected = false;
        if (m_closed.compare_exchange_strong(expected, true)) {
            if (m_log_level >= LogLevel::Info) {
                std::cout << "[cloudflared] Closing tunnel process: " << m_public_url << std::endl;
            }
            if (m_process) {
                m_process->stop();
            }
        }
    }

    bool is_closed() const override {
        return m_closed.load();
    }

    void wait() override {
        if (m_process) {
            m_process->wait_for_exit(-1);
        }
    }

private:
    std::string m_target_url;
    std::string m_public_url;
    std::unique_ptr<ProcessManager> m_process;
    LogLevel m_log_level;
    std::atomic<bool> m_closed{false};
};

} // namespace

std::shared_ptr<Tunnel> create_tunnel(const TunnelOptions& options) {
    std::string target_url = normalize_target_url(options);

    // Structured log helper: routes through consumer callback if set, else std::cout
    auto tunnel_log = [&options](LogLevel level, const std::string& msg) {
        if (options.log_level < level) return;
        if (options.log_callback) {
            options.log_callback(level, msg);
        } else {
            std::cout << msg << std::endl;
        }
    };

    tunnel_log(LogLevel::Info, "[cloudflared] Resolving cloudflared binary...");

    // 1. Resolve binary
    std::filesystem::path binary_path = ensure_cloudflared(
        options.custom_binary_path,
        options.cache_dir,
        PINNED_CLOUDFLARED_VERSION,
        false,
        nullptr,
        // Route binary manager lifecycle & download progress logs through tunnel_log
        [&tunnel_log](const std::string& msg) {
            tunnel_log(LogLevel::Info, msg);
        }
    );

    tunnel_log(LogLevel::Info, "[cloudflared] Binary ready: " + binary_path.string());

    std::string ext = binary_path.extension().string();
    bool is_shared_lib = (ext == ".dll" || ext == ".so" || ext == ".dylib");

    if (is_shared_lib) {
        tunnel_log(LogLevel::Info, "[cloudflared] Loading shared library " + binary_path.filename().string() + " into process...");

        auto api = CloudflaredLibApi::load(binary_path);

        if (api->set_silent_mode) {
            api->set_silent_mode(options.log_level == LogLevel::Silent ? 1 : 0);
        }

        int init_res = api->init();
        if (init_res != 0) {
            throw CloudflaredStartupError("CloudflaredInit returned error code " + std::to_string(init_res));
        }

        if (options.token && !options.token->empty()) {
            if (!api->run) {
                throw CloudflaredStartupError("CloudflaredRun symbol not found for token-based tunnel");
            }
            std::string cmd = "cloudflared tunnel run --token " + *options.token;
            if (options.protocol && !options.protocol->empty()) {
                cmd += " --protocol " + *options.protocol;
            }
            int run_res = api->run(cmd.data());
            if (run_res != 0) {
                throw CloudflaredStartupError("CloudflaredRun returned error code " + std::to_string(run_res));
            }
        } else if (options.protocol && !options.protocol->empty() && api->run) {
            std::string cmd = "cloudflared tunnel --url " + target_url + " --no-autoupdate --protocol " + *options.protocol;
            int run_res = api->run(cmd.data());
            if (run_res != 0) {
                throw CloudflaredStartupError("CloudflaredRun returned error code " + std::to_string(run_res));
            }
        } else if (options.port) {
            int q_res = api->start_quick_tunnel(static_cast<int>(*options.port));
            if (q_res != 0) {
                throw CloudflaredStartupError("CloudflaredStartQuickTunnel returned error code " + std::to_string(q_res));
            }
        } else if (options.url && !options.url->empty()) {
            if (!api->run) {
                throw CloudflaredStartupError("CloudflaredRun symbol not found for URL-based tunnel");
            }
            std::string cmd = "cloudflared tunnel --url " + *options.url + " --no-autoupdate";
            if (options.protocol && !options.protocol->empty()) {
                cmd += " --protocol " + *options.protocol;
            }
            int run_res = api->run(cmd.data());
            if (run_res != 0) {
                throw CloudflaredStartupError("CloudflaredRun returned error code " + std::to_string(run_res));
            }
        } else {
            throw InvalidTunnelOptionsError("Either 'port', 'url', or 'token' must be specified in TunnelOptions");
        }

        // Wait for public URL within timeout
        auto start_time = std::chrono::steady_clock::now();
        std::string public_url;

        while (std::chrono::steady_clock::now() - start_time < options.timeout) {
            char* c_url = api->get_tunnel_url();
            if (c_url != nullptr) {
                if (c_url[0] != '\0') {
                    public_url = c_url;
                }
                if (api->free_string) {
                    api->free_string(c_url);
                }
            }

            if (!public_url.empty()) {
                break;
            }

            if (options.token && !options.token->empty() && options.url && !options.url->empty()) {
                if (api->get_tunnel_status && api->get_tunnel_status() == 1) {
                    public_url = *options.url;
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (public_url.empty()) {
            if (api->stop) {
                api->stop();
            }
            throw TunnelTimeoutError("Timed out waiting for Cloudflare tunnel URL after " +
                                     std::to_string(options.timeout.count()) + "ms");
        }

        tunnel_log(LogLevel::Info, "[cloudflared] Tunnel established: " + public_url);

        return std::make_shared<TunnelSharedLibImpl>(target_url, public_url, api, options.log_level);
    }

    // 2. Prepare process arguments
    std::vector<std::string> args;
    if (options.token && !options.token->empty()) {
        args = { "tunnel", "run", "--token", *options.token };
        if (!target_url.empty()) {
            args.push_back("--url");
            args.push_back(target_url);
        }
    } else {
        args = {
            "tunnel",
            "--url",
            target_url,
            "--no-autoupdate"
        };
    }

    if (options.protocol && !options.protocol->empty()) {
        args.push_back("--protocol");
        args.push_back(*options.protocol);
    }

    for (const auto& extra : options.extra_args) {
        args.push_back(extra);
    }

    auto url_promise = std::make_shared<std::promise<std::string>>();
    std::future<std::string> url_future = url_promise->get_future();
    auto promise_set = std::make_shared<std::atomic<bool>>(false);
    auto last_error_line = std::make_shared<std::string>();
    auto error_mutex = std::make_shared<std::mutex>();

    auto handle_line = [options, url_promise, promise_set, last_error_line, error_mutex, tunnel_log](const std::string& line) {
        tunnel_log(LogLevel::Debug, "[cloudflared:raw] " + line);

        {
            std::lock_guard<std::mutex> lock(*error_mutex);
            *last_error_line = line;
        }

        if (!promise_set->load()) {
            auto url_opt = OutputParser::extract_tunnel_url(line);
            if (url_opt) {
                bool expected = false;
                if (promise_set->compare_exchange_strong(expected, true)) {
                    url_promise->set_value(*url_opt);
                }
            } else if (options.token && !options.token->empty()) {
                auto conn_opt = OutputParser::extract_connection(line);
                if (conn_opt || line.find("Registered tunnel connection") != std::string::npos) {
                    bool expected = false;
                    if (promise_set->compare_exchange_strong(expected, true)) {
                        std::string res_url = options.url ? *options.url : "https://tunnel.cloudflare.com";
                        url_promise->set_value(res_url);
                    }
                }
            }
        }
    };

    auto handle_exit = [url_promise, promise_set, last_error_line, error_mutex](int exit_code) {
        bool expected = false;
        if (promise_set->compare_exchange_strong(expected, true)) {
            std::string err;
            {
                std::lock_guard<std::mutex> lock(*error_mutex);
                err = *last_error_line;
            }
            std::string msg = "Process exited with code " + std::to_string(exit_code);
            if (!err.empty()) {
                msg += ": " + err;
            }
            url_promise->set_exception(std::make_exception_ptr(TunnelProcessError(msg)));
        }
    };

    auto process = std::make_unique<ProcessManager>(
        binary_path,
        args,
        handle_line, // stdout
        handle_line, // stderr (cloudflared outputs logs here)
        handle_exit  // on_exit
    );

    tunnel_log(LogLevel::Info, "[cloudflared] Starting tunnel process for " + target_url + "...");

    process->start();

    // Wait for public URL within timeout
    std::future_status status = url_future.wait_for(options.timeout);
    if (status == std::future_status::timeout) {
        process->stop();
        throw TunnelTimeoutError("Timed out waiting for public URL after " +
                                 std::to_string(options.timeout.count()) + "ms");
    }

    std::string public_url = url_future.get(); // May throw if process exited before URL

    tunnel_log(LogLevel::Info, "[cloudflared] Tunnel established: " + public_url);

    return std::make_shared<TunnelProcessImpl>(target_url, public_url, std::move(process), options.log_level);
}

} // namespace cloudflared
