#pragma once

#include <stdexcept>
#include <string>

namespace cloudflared {

/**
 * @brief Base exception for all Cloudflared library errors.
 */
class CloudflaredError : public std::runtime_error {
public:
    explicit CloudflaredError(const std::string& message)
        : std::runtime_error(message) {}
};

/**
 * @brief Thrown when running on an unsupported operating system or architecture.
 */
class UnsupportedPlatformError : public CloudflaredError {
public:
    explicit UnsupportedPlatformError(const std::string& message)
        : CloudflaredError("Unsupported platform: " + message) {}
};

/**
 * @brief Thrown when downloading the cloudflared binary fails.
 */
class BinaryDownloadError : public CloudflaredError {
public:
    explicit BinaryDownloadError(const std::string& message)
        : CloudflaredError("Binary download failed: " + message) {}
};

/**
 * @brief Thrown when SHA256 checksum verification fails.
 */
class BinaryVerificationError : public CloudflaredError {
public:
    BinaryVerificationError(const std::string& expected, const std::string& actual)
        : CloudflaredError("Cloudflared binary verification failed.\n"
                           "Expected SHA256: " + expected + "\n"
                           "Actual SHA256:   " + actual)
        , m_expected(expected)
        , m_actual(actual) {}

    const std::string& expected() const noexcept { return m_expected; }
    const std::string& actual() const noexcept { return m_actual; }

private:
    std::string m_expected;
    std::string m_actual;
};

/**
 * @brief Thrown when cloudflared fails to launch or encounters a startup error.
 */
class CloudflaredStartupError : public CloudflaredError {
public:
    explicit CloudflaredStartupError(const std::string& message)
        : CloudflaredError("Cloudflared startup failed: " + message) {}
};

/**
 * @brief Thrown when waiting for a tunnel public URL times out.
 */
class TunnelTimeoutError : public CloudflaredError {
public:
    explicit TunnelTimeoutError(const std::string& message)
        : CloudflaredError("Tunnel timeout: " + message) {}
};

/**
 * @brief Thrown when the cloudflared child process terminates unexpectedly.
 */
class TunnelProcessError : public CloudflaredError {
public:
    explicit TunnelProcessError(const std::string& message)
        : CloudflaredError("Tunnel process error: " + message) {}
};

/**
 * @brief Thrown when tunnel options are invalid (e.g. invalid port, malformed URL).
 */
class InvalidTunnelOptionsError : public CloudflaredError {
public:
    explicit InvalidTunnelOptionsError(const std::string& message)
        : CloudflaredError("Invalid tunnel options: " + message) {}
};

} // namespace cloudflared
