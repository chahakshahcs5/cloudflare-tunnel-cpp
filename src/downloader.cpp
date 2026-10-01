#include "cloudflared/downloader.hpp"
#include "cloudflared/error.hpp"
#include <fstream>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#if __has_include(<curl/curl.h>)
#include <curl/curl.h>
#define HAS_LIBCURL 1
#endif
#endif

namespace cloudflared {

static CustomDownloader g_custom_downloader = nullptr;

void set_custom_downloader(CustomDownloader downloader) {
    g_custom_downloader = std::move(downloader);
}

#if defined(_WIN32)

namespace {

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool is_https = true;
};

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), size_needed, nullptr, nullptr);
    return str;
}

ParsedUrl parse_url(const std::string& url_str) {
    std::wstring wurl(url_str.begin(), url_str.end());

    URL_COMPONENTS url_comp{};
    url_comp.dwStructSize = sizeof(url_comp);
    url_comp.dwHostNameLength = static_cast<DWORD>(-1);
    url_comp.dwUrlPathLength = static_cast<DWORD>(-1);
    url_comp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.length()), 0, &url_comp)) {
        throw BinaryDownloadError("Failed to parse URL: " + url_str);
    }

    ParsedUrl parsed;
    parsed.host = std::wstring(url_comp.lpszHostName, url_comp.dwHostNameLength);
    parsed.path = std::wstring(url_comp.lpszUrlPath, url_comp.dwUrlPathLength);
    if (url_comp.dwExtraInfoLength > 0 && url_comp.lpszExtraInfo) {
        parsed.path += std::wstring(url_comp.lpszExtraInfo, url_comp.dwExtraInfoLength);
    }
    parsed.port = url_comp.nPort;
    parsed.is_https = (url_comp.nScheme == INTERNET_SCHEME_HTTPS);
    return parsed;
}

void winhttp_download_stream(
    const std::string& initial_url,
    std::ostream& out_stream,
    std::function<void(size_t downloaded, size_t total)> progress_callback
) {
    HINTERNET hSession = WinHttpOpen(
        L"cloudflared-cpp/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession) {
        throw BinaryDownloadError("WinHttpOpen failed with error " + std::to_string(GetLastError()));
    }

    std::string current_url = initial_url;
    constexpr int MAX_REDIRECTS = 10;
    int redirect_count = 0;

    while (redirect_count < MAX_REDIRECTS) {
        ParsedUrl parsed = parse_url(current_url);

        HINTERNET hConnect = WinHttpConnect(
            hSession,
            parsed.host.c_str(),
            parsed.port,
            0
        );

        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            throw BinaryDownloadError("WinHttpConnect failed for host " + wstring_to_utf8(parsed.host));
        }

        DWORD flags = parsed.is_https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            parsed.path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags
        );

        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            throw BinaryDownloadError("WinHttpOpenRequest failed with error " + std::to_string(GetLastError()));
        }

        // Disable automatic redirect in WinHTTP to handle cross-scheme/cross-host explicitly
        DWORD redirect_option = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_option, sizeof(redirect_option));

        BOOL sent = WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
        );

        if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD err = GetLastError();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            throw BinaryDownloadError("WinHttp request/response failed with error " + std::to_string(err));
        }

        DWORD status_code = 0;
        DWORD status_code_size = sizeof(status_code);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_code_size,
            WINHTTP_NO_HEADER_INDEX
        );

        // Handle redirects (301, 302, 303, 307, 308)
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
            DWORD loc_size = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &loc_size, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && loc_size > 0) {
                std::vector<wchar_t> loc_buf(loc_size / sizeof(wchar_t) + 1, 0);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc_buf.data(), &loc_size, WINHTTP_NO_HEADER_INDEX)) {
                    std::wstring wloc(loc_buf.data());
                    current_url = wstring_to_utf8(wloc);
                    redirect_count++;
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    continue; // Loop with redirected URL
                }
            }
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            throw BinaryDownloadError("Redirect received without Location header");
        }

        if (status_code < 200 || status_code >= 300) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            throw BinaryDownloadError("HTTP request failed with status code " + std::to_string(status_code));
        }

        // Query Content-Length for progress reporting
        DWORD content_length = 0;
        DWORD cl_size = sizeof(content_length);
        bool has_total = WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &content_length,
            &cl_size,
            WINHTTP_NO_HEADER_INDEX
        ) != FALSE;

        size_t total_bytes = has_total ? content_length : 0;
        size_t downloaded_bytes = 0;

        constexpr DWORD CHUNK_SIZE = 64 * 1024; // 64 KB streaming buffer
        std::vector<char> buffer(CHUNK_SIZE);

        DWORD bytes_read = 0;
        do {
            DWORD bytes_available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) {
                break;
            }
            if (bytes_available == 0) {
                break;
            }

            DWORD to_read = (bytes_available < CHUNK_SIZE) ? bytes_available : CHUNK_SIZE;
            if (!WinHttpReadData(hRequest, buffer.data(), to_read, &bytes_read)) {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                throw BinaryDownloadError("WinHttpReadData failed with error " + std::to_string(GetLastError()));
            }

            if (bytes_read > 0) {
                out_stream.write(buffer.data(), bytes_read);
                downloaded_bytes += bytes_read;
                if (progress_callback) {
                    progress_callback(downloaded_bytes, total_bytes);
                }
            }
        } while (bytes_read > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    WinHttpCloseHandle(hSession);
    throw BinaryDownloadError("Too many HTTP redirects");
}

} // namespace

void download_file(
    const std::string& url,
    const std::filesystem::path& destination,
    std::function<void(size_t downloaded, size_t total)> progress_callback
) {
    if (g_custom_downloader) {
        if (g_custom_downloader(url, destination, progress_callback)) {
            if (std::filesystem::exists(destination) && std::filesystem::file_size(destination) > 0) {
                return;
            }
        }
    }

    std::filesystem::create_directories(destination.parent_path());
    std::ofstream file(destination, std::ios::binary);
    if (!file.is_open()) {
        throw BinaryDownloadError("Cannot open destination file for writing: " + destination.string());
    }

    try {
        winhttp_download_stream(url, file, progress_callback);
        file.close();
    } catch (...) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        throw;
    }
}

std::string download_string(const std::string& url) {
    std::ostringstream oss;
    winhttp_download_stream(url, oss, nullptr);
    return oss.str();
}

#else

// POSIX / Linux / macOS implementation using libcurl
#ifdef HAS_LIBCURL
namespace {

size_t curl_write_stream_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::ostream* stream = static_cast<std::ostream*>(userp);
    stream->write(static_cast<const char*>(contents), total);
    return total;
}

void curl_download_stream(
    const std::string& url,
    std::ostream& out_stream,
    std::function<void(size_t downloaded, size_t total)> progress_callback
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw BinaryDownloadError("Failed to initialize libcurl");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cloudflared-cpp/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_stream_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_stream);

    // Wire progress callback via CURLOPT_XFERINFOFUNCTION
    if (progress_callback) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
            +[](void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) -> int {
                auto* cb = static_cast<std::function<void(size_t, size_t)>*>(clientp);
                if (*cb && dlnow > 0) {
                    (*cb)(static_cast<size_t>(dlnow), static_cast<size_t>(dltotal));
                }
                return 0;
            }
        );
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_callback);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string err_msg = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw BinaryDownloadError("Curl download failed: " + err_msg);
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code < 200 || http_code >= 300) {
        throw BinaryDownloadError("HTTP request failed with status code " + std::to_string(http_code));
    }
}

} // namespace
#endif // HAS_LIBCURL


void download_file(
    const std::string& url,
    const std::filesystem::path& destination,
    std::function<void(size_t downloaded, size_t total)> progress_callback
) {
    std::filesystem::create_directories(destination.parent_path());

#ifdef HAS_LIBCURL
    {
        std::ofstream file(destination, std::ios::binary);
        if (file.is_open()) {
            try {
                curl_download_stream(url, file, progress_callback);
                file.close();
                if (std::filesystem::exists(destination) && std::filesystem::file_size(destination) > 0) {
                    return;
                }
            } catch (...) {
                file.close();
                std::error_code ec;
                std::filesystem::remove(destination, ec);
            }
        }
    }
#endif

    // 1. Try original CLI curl / wget methods first (works on standard Linux/Termux environments)
    std::string found_curl;
    const std::vector<std::string> curl_paths = {
        "curl",
        "/bin/curl",
        "/system/bin/curl",
        "/system/xbin/curl",
        "/data/data/com.termux/files/usr/bin/curl"
    };
    for (const auto& p : curl_paths) {
        std::string test_cmd = p + " --version >/dev/null 2>&1";
        if (std::system(test_cmd.c_str()) == 0) {
            found_curl = p;
            break;
        }
    }

    int ret = -1;
    if (!found_curl.empty()) {
        std::string cmd = found_curl + " -f -sSL -o \"" + destination.string() + "\" \"" + url + "\"";
        ret = std::system(cmd.c_str());
    } else {
        bool has_wget = (std::system("command -v wget >/dev/null 2>&1") == 0);
        if (has_wget) {
            std::string cmd = "wget -q -O \"" + destination.string() + "\" \"" + url + "\"";
            ret = std::system(cmd.c_str());
        }
    }

    if (ret == 0 && std::filesystem::exists(destination) && std::filesystem::file_size(destination) > 0) {
        if (progress_callback) {
            size_t sz = std::filesystem::file_size(destination);
            progress_callback(sz, sz);
        }
        return;
    }

    // 2. Fallback: If original download method failed or is unavailable, use custom downloader (Android HttpURLConnection)
    if (g_custom_downloader) {
        if (g_custom_downloader(url, destination, progress_callback)) {
            if (std::filesystem::exists(destination) && std::filesystem::file_size(destination) > 0) {
                if (progress_callback) {
                    size_t sz = std::filesystem::file_size(destination);
                    progress_callback(sz, sz);
                }
                return;
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove(destination, ec);
    throw BinaryDownloadError("Failed to download cloudflared binary: " + url);
}

std::string download_string(const std::string& url) {
#ifdef HAS_LIBCURL
    try {
        std::ostringstream oss;
        curl_download_stream(url, oss, nullptr);
        return oss.str();
    } catch (...) {}
#endif
    std::string cmd = "curl -f -sSL \"" + url + "\" 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        throw BinaryDownloadError("Failed to execute curl CLI for URL: " + url);
    }
    char buf[4096];
    std::string result;
    while (fgets(buf, sizeof(buf), fp) != nullptr) {
        result += buf;
    }
    pclose(fp);
    return result;
}

#endif // _WIN32

} // namespace cloudflared
