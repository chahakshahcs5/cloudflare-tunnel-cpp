#include "cloudflared/process.hpp"
#include "cloudflared/error.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cloudflared {

#if defined(_WIN32)

namespace {

std::mutex g_global_process_mutex;
std::vector<HANDLE> g_active_process_handles;
bool g_atexit_registered = false;

void cleanup_active_processes() {
    std::lock_guard<std::mutex> lock(g_global_process_mutex);
    for (HANDLE h : g_active_process_handles) {
        if (h && h != INVALID_HANDLE_VALUE) {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    }
    g_active_process_handles.clear();
}

void register_global_cleanup() {
    std::lock_guard<std::mutex> lock(g_global_process_mutex);
    if (!g_atexit_registered) {
        std::atexit(cleanup_active_processes);
        g_atexit_registered = true;
    }
}

void track_process(HANDLE h) {
    std::lock_guard<std::mutex> lock(g_global_process_mutex);
    g_active_process_handles.push_back(h);
}

void untrack_process(HANDLE h) {
    std::lock_guard<std::mutex> lock(g_global_process_mutex);
    for (auto it = g_active_process_handles.begin(); it != g_active_process_handles.end(); ++it) {
        if (*it == h) {
            g_active_process_handles.erase(it);
            break;
        }
    }
}

std::wstring quote_arg(const std::string& arg) {
    std::wstring warg(arg.begin(), arg.end());
    if (warg.find(L' ') == std::wstring::npos && warg.find(L'\"') == std::wstring::npos) {
        return warg;
    }

    std::wstring quoted = L"\"";
    for (size_t i = 0; i < warg.length(); ++i) {
        if (warg[i] == L'\"') {
            quoted += L"\\\"";
        } else {
            quoted += warg[i];
        }
    }
    quoted += L"\"";
    return quoted;
}

} // namespace

class ProcessManager::Impl {
public:
    Impl(
        std::filesystem::path executable,
        std::vector<std::string> arguments,
        OutputCallback on_stdout,
        OutputCallback on_stderr,
        ExitCallback on_exit
    )
        : m_executable(std::move(executable))
        , m_arguments(std::move(arguments))
        , m_on_stdout(std::move(on_stdout))
        , m_on_stderr(std::move(on_stderr))
        , m_on_exit(std::move(on_exit))
    {
        register_global_cleanup();
    }

    ~Impl() {
        stop();
    }

    void start() {
        if (m_is_running.load()) {
            return;
        }

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
        HANDLE hStderrRead = nullptr, hStderrWrite = nullptr;

        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
            throw CloudflaredStartupError("Failed to create stdout pipe");
        }
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

        if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
            CloseHandle(hStdoutRead);
            CloseHandle(hStdoutWrite);
            throw CloudflaredStartupError("Failed to create stderr pipe");
        }
        SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStderrWrite;
        si.hStdInput = INVALID_HANDLE_VALUE;

        std::wstring cmdline = quote_arg(m_executable.string());
        for (const auto& arg : m_arguments) {
            cmdline += L" " + quote_arg(arg);
        }

        std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
        cmd_buf.push_back(L'\0');

        PROCESS_INFORMATION pi{};
        DWORD creation_flags = CREATE_NO_WINDOW;

        BOOL success = CreateProcessW(
            nullptr,
            cmd_buf.data(),
            nullptr,
            nullptr,
            TRUE,
            creation_flags,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrWrite);

        if (!success) {
            CloseHandle(hStdoutRead);
            CloseHandle(hStderrRead);
            DWORD err = GetLastError();
            throw CloudflaredStartupError("CreateProcessW failed with error " + std::to_string(err));
        }

        m_hProcess = pi.hProcess;
        m_pid = static_cast<int64_t>(pi.dwProcessId);
        m_is_running.store(true);

        CloseHandle(pi.hThread);
        track_process(m_hProcess);

        // Spawn reader threads
        m_stdout_thread = std::thread([this, hStdoutRead]() {
            read_stream(hStdoutRead, m_on_stdout);
            CloseHandle(hStdoutRead);
        });

        m_stderr_thread = std::thread([this, hStderrRead]() {
            read_stream(hStderrRead, m_on_stderr);
            CloseHandle(hStderrRead);
        });

        // Spawn watcher thread for exit
        m_watch_thread = std::thread([this]() {
            WaitForSingleObject(m_hProcess, INFINITE);

            DWORD exit_code = 0;
            GetExitCodeProcess(m_hProcess, &exit_code);

            m_is_running.store(false);
            if (m_on_exit) {
                m_on_exit(static_cast<int>(exit_code));
            }
        });
    }

    void stop() {
        if (m_is_running.exchange(false)) {
            if (m_pid.has_value() && *m_pid > 0) {
                std::string cmd = "taskkill /F /T /PID " + std::to_string(*m_pid) + " >nul 2>&1";
                system(cmd.c_str());
            }
            if (m_hProcess && m_hProcess != INVALID_HANDLE_VALUE) {
                TerminateProcess(m_hProcess, 0);
                WaitForSingleObject(m_hProcess, 2000);
            }
        }

        if (m_hProcess && m_hProcess != INVALID_HANDLE_VALUE) {
            untrack_process(m_hProcess);
            CloseHandle(m_hProcess);
            m_hProcess = nullptr;
        }

        // Always join threads to prevent std::terminate() on destruction
        auto self_id = std::this_thread::get_id();
        if (m_stdout_thread.joinable() && m_stdout_thread.get_id() != self_id) {
            m_stdout_thread.join();
        }
        if (m_stderr_thread.joinable() && m_stderr_thread.get_id() != self_id) {
            m_stderr_thread.join();
        }
        if (m_watch_thread.joinable() && m_watch_thread.get_id() != self_id) {
            m_watch_thread.join();
        }

        m_pid = std::nullopt;
    }

    bool is_running() const {
        return m_is_running.load();
    }

    std::optional<int64_t> pid() const {
        return m_pid;
    }

    bool wait_for_exit(int timeout_ms) {
        if (!m_hProcess) return true;
        DWORD dwTimeout = (timeout_ms < 0) ? INFINITE : static_cast<DWORD>(timeout_ms);
        DWORD result = WaitForSingleObject(m_hProcess, dwTimeout);
        return (result == WAIT_OBJECT_0);
    }

private:
    void read_stream(HANDLE hPipe, const OutputCallback& callback) {
        constexpr DWORD BUF_SIZE = 4096;
        std::vector<char> buffer(BUF_SIZE);
        std::string line_buffer;

        DWORD bytes_read = 0;
        while (ReadFile(hPipe, buffer.data(), BUF_SIZE, &bytes_read, nullptr) && bytes_read > 0) {
            for (DWORD i = 0; i < bytes_read; ++i) {
                char ch = buffer[i];
                if (ch == '\n') {
                    if (!line_buffer.empty() && line_buffer.back() == '\r') {
                        line_buffer.pop_back();
                    }
                    if (callback) {
                        callback(line_buffer);
                    }
                    line_buffer.clear();
                } else {
                    line_buffer += ch;
                }
            }
        }

        if (!line_buffer.empty() && callback) {
            if (line_buffer.back() == '\r') line_buffer.pop_back();
            callback(line_buffer);
        }
    }

    std::filesystem::path m_executable;
    std::vector<std::string> m_arguments;
    OutputCallback m_on_stdout;
    OutputCallback m_on_stderr;
    ExitCallback m_on_exit;

    HANDLE m_hProcess = nullptr;
    std::optional<int64_t> m_pid;
    std::atomic<bool> m_is_running{false};

    std::thread m_stdout_thread;
    std::thread m_stderr_thread;
    std::thread m_watch_thread;
};

#else

// POSIX implementation
class ProcessManager::Impl {
public:
    Impl(
        std::filesystem::path executable,
        std::vector<std::string> arguments,
        OutputCallback on_stdout,
        OutputCallback on_stderr,
        ExitCallback on_exit
    )
        : m_executable(std::move(executable))
        , m_arguments(std::move(arguments))
        , m_on_stdout(std::move(on_stdout))
        , m_on_stderr(std::move(on_stderr))
        , m_on_exit(std::move(on_exit))
    {}

    ~Impl() {
        stop();
    }

    void start() {
        if (m_is_running.load()) {
            return;
        }

        int stdout_pipe[2];
        int stderr_pipe[2];

        if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            throw CloudflaredStartupError("Failed to create pipes");
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(stdout_pipe[0]); close(stdout_pipe[1]);
            close(stderr_pipe[0]); close(stderr_pipe[1]);
            throw CloudflaredStartupError("fork failed");
        }

        if (pid == 0) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);

            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);

            close(stdout_pipe[1]);
            close(stderr_pipe[1]);

            std::vector<char*> args;
            std::string exec_str = m_executable.string();
            args.push_back(const_cast<char*>(exec_str.c_str()));
            for (auto& a : m_arguments) {
                args.push_back(const_cast<char*>(a.c_str()));
            }
            args.push_back(nullptr);

            execvp(exec_str.c_str(), args.data());
            _exit(127);
        }

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        m_child_pid = pid;
        m_pid = static_cast<int64_t>(pid);
        m_is_running.store(true);

        int out_fd = stdout_pipe[0];
        int err_fd = stderr_pipe[0];

        m_stdout_thread = std::thread([this, out_fd]() {
            read_fd(out_fd, m_on_stdout);
            close(out_fd);
        });

        m_stderr_thread = std::thread([this, err_fd]() {
            read_fd(err_fd, m_on_stderr);
            close(err_fd);
        });

        m_watch_thread = std::thread([this, pid]() {
            int status = 0;
            waitpid(pid, &status, 0);
            m_is_running.store(false);
            if (m_on_exit) {
                int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                m_on_exit(code);
            }
        });
    }

    void stop() {
        if (m_is_running.exchange(false)) {
            if (m_child_pid > 0) {
                kill(m_child_pid, SIGINT);
                for (int i = 0; i < 20; ++i) {
                    int status = 0;
                    pid_t res = waitpid(m_child_pid, &status, WNOHANG);
                    if (res == m_child_pid) {
                        m_child_pid = -1;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (m_child_pid > 0) {
                    kill(m_child_pid, SIGKILL);
                    int status = 0;
                    waitpid(m_child_pid, &status, 0);
                    m_child_pid = -1;
                }
            }
        }

        auto self_id = std::this_thread::get_id();
        if (m_stdout_thread.joinable() && m_stdout_thread.get_id() != self_id) m_stdout_thread.join();
        if (m_stderr_thread.joinable() && m_stderr_thread.get_id() != self_id) m_stderr_thread.join();
        if (m_watch_thread.joinable() && m_watch_thread.get_id() != self_id) m_watch_thread.join();

        m_pid = std::nullopt;
    }

    bool is_running() const { return m_is_running.load(); }
    std::optional<int64_t> pid() const { return m_pid; }

    bool wait_for_exit(int timeout_ms) {
        if (m_child_pid <= 0) return true;
        int elapsed = 0;
        while (m_is_running.load()) {
            if (timeout_ms >= 0 && elapsed >= timeout_ms) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            elapsed += 50;
        }
        return true;
    }

private:
    void read_fd(int fd, const OutputCallback& callback) {
        char buffer[4096];
        std::string line_buffer;
        ssize_t bytes_read = 0;

        while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            for (ssize_t i = 0; i < bytes_read; ++i) {
                char ch = buffer[i];
                if (ch == '\n') {
                    if (!line_buffer.empty() && line_buffer.back() == '\r') line_buffer.pop_back();
                    if (callback) callback(line_buffer);
                    line_buffer.clear();
                } else {
                    line_buffer += ch;
                }
            }
        }
        if (!line_buffer.empty() && callback) {
            if (line_buffer.back() == '\r') line_buffer.pop_back();
            callback(line_buffer);
        }
    }

    std::filesystem::path m_executable;
    std::vector<std::string> m_arguments;
    OutputCallback m_on_stdout;
    OutputCallback m_on_stderr;
    ExitCallback m_on_exit;

    pid_t m_child_pid = -1;
    std::optional<int64_t> m_pid;
    std::atomic<bool> m_is_running{false};

    std::thread m_stdout_thread;
    std::thread m_stderr_thread;
    std::thread m_watch_thread;
};

#endif

ProcessManager::ProcessManager(
    std::filesystem::path executable,
    std::vector<std::string> arguments,
    OutputCallback on_stdout,
    OutputCallback on_stderr,
    ExitCallback on_exit
)
    : m_impl(std::make_unique<Impl>(
        std::move(executable),
        std::move(arguments),
        std::move(on_stdout),
        std::move(on_stderr),
        std::move(on_exit)
    ))
{}

ProcessManager::~ProcessManager() = default;
ProcessManager::ProcessManager(ProcessManager&&) noexcept = default;
ProcessManager& ProcessManager::operator=(ProcessManager&&) noexcept = default;

void ProcessManager::start() { m_impl->start(); }
void ProcessManager::stop() { m_impl->stop(); }
bool ProcessManager::is_running() const { return m_impl->is_running(); }
std::optional<int64_t> ProcessManager::pid() const { return m_impl->pid(); }
bool ProcessManager::wait_for_exit(int timeout_ms) { return m_impl->wait_for_exit(timeout_ms); }

} // namespace cloudflared
