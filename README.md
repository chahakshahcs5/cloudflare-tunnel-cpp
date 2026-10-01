# cloudflare-tunnel (C++ Edition)

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20Android-lightgrey.svg)]()
[![Security](https://img.shields.io/badge/security-SHA256%20Verified-success.svg)]()

A standalone, production-quality modern C++ (C++20) library and CLI that makes it extremely easy for any C++ application to expose a local HTTP server or service through **Cloudflare Tunnel using `cloudflared`**.

It completely abstracts `cloudflared` from the consuming developer:
- **Zero-config setup**: Automatically determines OS & architecture, downloads official binary on demand, and caches it.
- **Strict security**: Every binary download is verified against official pinned SHA256 checksums before execution.
- **Automatic process lifecycle**: Asynchronous stream readers, public URL detection, and signal/atexit handlers preventing orphaned zombie processes.
- **Idempotent shutdown**: Clean, safe, repeatable `close()` calls via RAII.
- **Cross-platform**: Windows (WinHTTP, zero external dependencies), Linux (libcurl), macOS, and Android (NDK native / Termux / Android curl).

---

## Quickstart

### 1. Minimal Example

```cpp
#include <cloudflared/cloudflared.hpp>
#include <iostream>

int main() {
    // 1. Expose local port 3000 (normalized to http://localhost:3000)
    auto tunnel = cloudflared::create_tunnel({ .port = 3000 });

    // 2. Access the public Cloudflare HTTPS URL
    std::cout << "Public URL: " << tunnel->url() << std::endl;

    // 3. Application continues running...
    // tunnel->wait();

    // 4. Graceful, idempotent shutdown
    tunnel->close();
    return 0;
}
```

The developer does **NOT** need to:
- Manually install or configure `cloudflared`
- Determine system architecture (x64, arm64, etc.)
- Configure `PATH`
- Set `chmod +x` permissions on Unix
- Manage child process pipes or parse raw regex output
- Clean up lingering background processes on exit

---

## Architecture

```mermaid
flowchart TD
    App["Consumer C++ Application"]

    App -->|"cloudflared::create_tunnel(options)"| TunnelAPI["libcloudflared::Tunnel API"]

    subgraph Lib["libcloudflared Lifecycle Engine"]
        direction TB
        TunnelAPI --> BinMgr["Binary Manager"]

        subgraph Resolution["Resolution & Verification"]
            direction LR
            Platform["Platform Resolver<br/>(OS & Architecture)"]
            Cache["Cache Resolver<br/>(%LOCALAPPDATA% / ~/.cache)"]
            Manifest["Release Manifest<br/>(Pinned Checksums)"]
            Downloader["HTTP Downloader<br/>(WinHTTP / libcurl)"]
            Verifier["SHA-256 Verifier<br/>(Cryptographic Check)"]
        end

        BinMgr --> Resolution
        Resolution --> ProcessMgr["Process Manager<br/>(Async I/O & Exit Watcher)"]
        ProcessMgr --> Parser["Output Parser<br/>(Regex URL Extraction)"]
        ProcessMgr --> Cleanup["Signal & atexit Registry<br/>(Zero Zombie Processes)"]
    end

    Lib -->|"Spawns Process"| Binary["Official cloudflared Binary"]
    Binary -->|"Establishes Tunnel"| URL["Public HTTPS URL<br/>(https://*.trycloudflare.com)"]
```

---

## Configuration Options

```cpp
cloudflared::TunnelOptions options;
options.port = 8080;                                      // Target port (1 - 65535)
// OR:
options.url = "http://localhost:8080";                   // Explicit URL

options.timeout = std::chrono::seconds(30);               // Timeout waiting for URL
options.log_level = cloudflared::LogLevel::Info;          // Silent, Info, or Debug
options.cache_dir = "C:/my/custom/cache";                // Optional cache override
options.extra_args = { "--metrics", "localhost:9090" };   // Additional cloudflared flags

auto tunnel = cloudflared::create_tunnel(options);
```

### Input Normalization & Validation
- Providing `.port = 3000` automatically normalizes to `"http://localhost:3000"`.
- Ports outside `1..65535` throw `cloudflared::InvalidTunnelOptionsError`.
- Missing target URL/port throws `cloudflared::InvalidTunnelOptionsError`.

---

## Binary Management & Security

### Pinned Official Release & SHA256 Verification
The library pins official Cloudflare release **`2026.8.3`** with hardcoded cryptographic SHA256 hashes:

| Platform | Architecture | Official Asset | SHA256 Checksum |
|---|---|---|---|
| **Windows** | x64 (amd64) | `cloudflared-windows-amd64.exe` | `83e726ed18ea78c5ad5213c4c3a3a27051393950d2bc8ed4de69bec12d14eaae` |
| **Windows** | x86 (386) | `cloudflared-windows-386.exe` | `bdfab00122a3c2a0772d3f176445f6baf0271fed71656d0902cbc23a0eea7048` |
| **Linux** | x64 (amd64) | `cloudflared-linux-amd64` | `f29324fe934d1e100617484c78deef803c4dc2cd351d645bbde42e96b4fccc5e` |
| **Linux** | arm64 | `cloudflared-linux-arm64` | `4bcfd35521a7cbc545ebfd5d57334a71ee180e2a64874981f374c81472118391` |
| **Linux** | arm | `cloudflared-linux-arm` | `7a7cac4ad4561ff55797eaf27aae1a0be37498c85502715bc87e3bad919d928c` |
| **Android** | arm64 (v8a) | `cloudflared-android-arm64.so` | `f97fb6c074cb3b8e7a2104211c99fb617afed4439719415a03763a50501b4c32` |
| **Android** | arm (v7a) | `cloudflared-android-arm.so` | `e4ec9ee4c5c3b33b66d30137130c0735654ed675d9eca4f644e8de94e8b97970` |
| **Android** | x64 (x86_64) | `cloudflared-android-amd64.so` | `fdff7f4dea3ce1f66877d692b40ff74c676bc2bf5d15fa2f3d622dc388a17d10` |
| **macOS** | x64 (amd64) | `cloudflared-darwin-amd64.tgz` | `936aa4ed783b0e191fac48e7140c34605b25d8d5c0495c3599c90e350ae6e4c4` |
| **macOS** | arm64 | `cloudflared-darwin-arm64.tgz` | `50a04624531e7a98ddb65f1223905e32f84e7488ed3ee8dadcd3260aa8932603` |

1. **Never executes unverified binaries**: The file is streamed to a temporary file (`.tmp`), verified against the official SHA256 checksum, and only moved atomically into place if the checksum matches.
2. **Auto-Purge on Tampering**: If verification fails, the invalid file is deleted immediately and a `cloudflared::BinaryVerificationError` is thrown.
3. **Atomic Installation**: File renames are atomic (`std::filesystem::rename`), preventing race conditions or partially written binaries.
4. **Thread-Safe**: In-process mutex prevents concurrent calls from double-downloading or corrupting the cache.

### Standard User Cache Location
- **Windows**: `%LOCALAPPDATA%\cloudflared-cpp\cloudflared\<version>\<platform>\<binary>`
- **macOS**: `~/Library/Caches/cloudflared-cpp/...`
- **Linux**: `$XDG_CACHE_HOME/cloudflared-cpp/...` (or `~/.cache/...`)
- **Override**: Set the `CLOUDFLARED_CACHE_DIR` environment variable to override.

---

## CLI Tool

A drop-in command-line binary `cloudflared-cli` is included:

```bash
# Print path to cached binary (downloads & verifies if missing)
cloudflared-cli bin

# List 30 latest releases from GitHub API
cloudflared-cli bin list

# Remove cached binary
cloudflared-cli bin remove

# Quick tunnel directly from CLI
cloudflared-cli quick 3000

# Transparent passthrough to official cloudflared
cloudflared-cli --version
cloudflared-cli tunnel --hello-world
```

---

## Building and Testing

### Prerequisites
- C++20 compliant compiler (MSVC 2019/2022/2026, GCC 10+, Clang 12+)
- CMake 3.20+

### Build
```bash
cmake -B build -S .
cmake --build build --config Release
```

### Run Tests
```bash
ctest --test-dir build -C Release --output-on-failure
```

### Run Examples
```bash
# Quick port tunnel
build/Release/quick_tunnel 8080

# URL tunnel
build/Release/url_tunnel

# Custom options demo
build/Release/custom_options
```

---

## Error Handling

All library exceptions derive from `cloudflared::CloudflaredError`:
- `UnsupportedPlatformError`: Running on an unsupported OS or CPU architecture.
- `BinaryDownloadError`: Network failure or HTTP error downloading binary.
- `BinaryVerificationError`: Checksum mismatch between downloaded file and pinned SHA256.
- `CloudflaredStartupError`: Subprocess failure during launch.
- `TunnelTimeoutError`: Timeout elapsed before public URL was emitted.
- `TunnelProcessError`: Child process crashed or exited unexpectedly.
- `InvalidTunnelOptionsError`: Invalid port numbers or malformed URLs.

---

## License

MIT License. See [LICENSE](LICENSE) for details.
