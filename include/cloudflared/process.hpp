#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cloudflared {

/**
 * @brief Manages execution and asynchronous I/O streaming of a child process.
 */
class ProcessManager {
public:
    using OutputCallback = std::function<void(const std::string& line)>;
    using ExitCallback = std::function<void(int exit_code)>;

    ProcessManager(
        std::filesystem::path executable,
        std::vector<std::string> arguments,
        OutputCallback on_stdout = nullptr,
        OutputCallback on_stderr = nullptr,
        ExitCallback on_exit = nullptr
    );

    ~ProcessManager();

    // Non-copyable, movable
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
    ProcessManager(ProcessManager&&) noexcept;
    ProcessManager& operator=(ProcessManager&&) noexcept;

    /**
     * @brief Starts the child process asynchronously.
     * @throws CloudflaredStartupError If the process fails to start.
     */
    void start();

    /**
     * @brief Gracefully terminates the child process and waits for exit.
     * Safe to call multiple times (idempotent).
     */
    void stop();

    /**
     * @brief Checks if the process is currently running.
     */
    bool is_running() const;

    /**
     * @brief Returns the OS Process ID (PID) if running.
     */
    std::optional<int64_t> pid() const;

    /**
     * @brief Waits up to timeout_ms for the process to exit.
     * @return true if exited, false if timeout elapsed.
     */
    bool wait_for_exit(int timeout_ms = -1);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cloudflared
