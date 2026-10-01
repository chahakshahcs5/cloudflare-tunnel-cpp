#include "cloudflared/cloudflared.hpp"
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

void print_help() {
    std::cout << "cloudflared CLI (C++ Edition)\n\n"
              << "Usage:\n"
              << "  cloudflared bin                     : Prints the path to the cached binary\n"
              << "  cloudflared bin remove              : Removes the cached binary\n"
              << "  cloudflared bin install [version]   : Installs the binary (pinned or specified)\n"
              << "  cloudflared bin list                : Lists latest releases from GitHub\n"
              << "  cloudflared bin help                : Prints this help message\n"
              << "  cloudflared quick <port> [--protocol http2] : Quickly exposes local port via Cloudflare tunnel\n"
              << "  cloudflared [args...]               : Passes any other arguments directly to cloudflared\n\n"
              << "Examples:\n"
              << "  cloudflared bin install             : Installs the pinned version\n"
              << "  cloudflared quick 3000              : Exposes http://localhost:3000 using HTTP/2\n"
              << "  cloudflared quick 3000 --protocol http2 : Exposes port using TCP HTTP/2 (firewall safe)\n"
              << "  cloudflared tunnel --hello-world    : Runs cloudflared with hello-world mode\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    try {
        if (!args.empty() && args[0] == "bin") {
            if (args.size() == 1) {
                auto path = cloudflared::ensure_cloudflared(
                    std::nullopt, std::nullopt, cloudflared::PINNED_CLOUDFLARED_VERSION, false,
                    nullptr,
                    [](const std::string& msg) { std::cerr << msg << std::endl; }
                );
                std::cout << path.string() << std::endl;
                return 0;
            }

            const std::string& sub = args[1];
            if (sub == "remove") {
                bool removed = cloudflared::remove_cached_cloudflared();
                if (removed) {
                    std::cout << "Removed cached cloudflared binary." << std::endl;
                } else {
                    std::cout << "No cached cloudflared binary found." << std::endl;
                }
                return 0;
            }

            if (sub == "install") {
                std::string ver = (args.size() >= 3) ? args[2] : cloudflared::PINNED_CLOUDFLARED_VERSION;
                std::cout << "Installing cloudflared (" << ver << ")..." << std::endl;
                auto path = cloudflared::ensure_cloudflared(
                    std::nullopt, std::nullopt, ver, true,
                    nullptr,
                    [](const std::string& msg) { std::cout << msg << std::endl; }
                );
                std::cout << "Installed to: " << path.string() << std::endl;
                return 0;
            }

            if (sub == "list") {
                std::cout << "Fetching releases from GitHub..." << std::endl;
                try {
                    std::string json = cloudflared::download_string("https://api.github.com/repos/cloudflare/cloudflared/releases");
                    // Print raw json or release tags
                    std::cout << "Available releases: Visit https://github.com/cloudflare/cloudflared/releases" << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to fetch release list: " << e.what() << std::endl;
                    return 1;
                }
                return 0;
            }

            if (sub == "help" || sub == "--help" || sub == "-h") {
                print_help();
                return 0;
            }

            std::cerr << "Unknown subcommand: " << sub << std::endl;
            print_help();
            return 1;
        }

        if (!args.empty() && args[0] == "quick") {
            if (args.size() < 2) {
                std::cerr << "Error: 'quick' requires a port or URL (e.g. 'cloudflared quick 3000')" << std::endl;
                return 1;
            }
            std::string target = args[1];
            cloudflared::TunnelOptions opts;
            opts.log_level = cloudflared::LogLevel::Info;

            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--protocol" && i + 1 < args.size()) {
                    opts.protocol = args[++i];
                } else {
                    opts.extra_args.push_back(args[i]);
                }
            }

            try {
                int port = std::stoi(target);
                if (port > 0 && port <= 65535) {
                    opts.port = static_cast<uint16_t>(port);
                } else {
                    opts.url = target;
                }
            } catch (...) {
                opts.url = target;
            }

            auto tunnel = cloudflared::create_tunnel(opts);
            std::cout << "\nPress Ctrl+C to close tunnel." << std::endl;
            tunnel->wait();
            return 0;
        }

        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            print_help();
            return 0;
        }

        // Passthrough mode: ensure binary is installed, then spawn with raw args
        auto binary_path = cloudflared::ensure_cloudflared(
            std::nullopt, std::nullopt, cloudflared::PINNED_CLOUDFLARED_VERSION, false,
            nullptr,
            [](const std::string& msg) { std::cerr << msg << std::endl; }
        );

#if defined(_WIN32)
        std::wstring cmdline = L"\"" + binary_path.wstring() + L"\"";
        for (const auto& arg : args) {
            std::wstring warg(arg.begin(), arg.end());
            cmdline += L" \"" + warg + L"\"";
        }

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        PROCESS_INFORMATION pi{};

        std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
        cmd_buf.push_back(L'\0');

        if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
            std::cerr << "Failed to launch cloudflared: error " << GetLastError() << std::endl;
            return 1;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return static_cast<int>(exit_code);
#else
        std::vector<char*> c_args;
        std::string exec_str = binary_path.string();
        c_args.push_back(const_cast<char*>(exec_str.c_str()));
        for (auto& a : args) {
            c_args.push_back(const_cast<char*>(a.c_str()));
        }
        c_args.push_back(nullptr);

        pid_t pid = fork();
        if (pid == 0) {
            execvp(exec_str.c_str(), c_args.data());
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif

    } catch (const cloudflared::CloudflaredError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
