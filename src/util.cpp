#include "util.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <unistd.h>

namespace util {

std::string now_http_date() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_POSIX_VERSION)
    gmtime_r(&t, &tm);
#else
    tm = *std::gmtime(&t);
#endif
    char buf[64];
    // Example: Sun, 06 Nov 1994 08:49:37 GMT
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buf);
}

bool send_all(int fd, const void* data, std::size_t size) {
    const char* p = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < size) {
        ssize_t rc = ::send(fd, p + sent, size - sent, 0);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (rc == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(rc);
    }
    return true;
}

std::optional<std::string> recv_http_request(int fd, std::size_t max_bytes) {
    std::string data;
    data.reserve(1024);

    std::array<char, 4096> buf{};
    while (data.size() < max_bytes) {
        ssize_t rc = ::recv(fd, buf.data(), buf.size(), 0);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return std::nullopt;
        }
        if (rc == 0) {
            break;
        }
        data.append(buf.data(), static_cast<std::size_t>(rc));
        // stop after end of headers
        if (data.find("\r\n\r\n") != std::string::npos) {
            return data;
        }
    }
    return std::nullopt;
}

static int hex_value(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return 10 + (c - 'a');
    if ('A' <= c && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string url_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size()) {
            int hi = hex_value(s[i + 1]);
            int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else if (c == '+') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string mime_type_from_path(const std::string& path) {
    auto dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return std::tolower(ch); });

    static const std::map<std::string, std::string> kMap = {
        {"html", "text/html; charset=utf-8"},
        {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},
        {"js", "application/javascript; charset=utf-8"},
        {"json", "application/json; charset=utf-8"},
        {"txt", "text/plain; charset=utf-8"},
        {"svg", "image/svg+xml"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"ico", "image/x-icon"},
        {"pdf", "application/pdf"},
        {"wasm", "application/wasm"},
    };

    auto it = kMap.find(ext);
    if (it != kMap.end()) return it->second;
    return "application/octet-stream";
}

std::string trim(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

} // namespace util
