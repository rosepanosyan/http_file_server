#include "http_server.h"

#include "thread_pool.h"
#include "util.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) {
    g_stop.store(true);
}

struct Fd {
    int fd{-1};

    Fd() = default;
    explicit Fd(int f) : fd(f) {}
    ~Fd() {
        if (fd >= 0) ::close(fd);
    }

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    Fd(Fd&& other) noexcept : fd(other.fd) { other.fd = -1; }
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) ::close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    int get() const { return fd; }
    int release() {
        int tmp = fd;
        fd = -1;
        return tmp;
    }
};

struct RequestLine {
    std::string method;
    std::string target;
    std::string version;
};

std::optional<RequestLine> parse_request_line(std::string_view request) {
    auto pos = request.find("\r\n");
    if (pos == std::string_view::npos) return std::nullopt;
    std::string_view line = request.substr(0, pos);

    std::istringstream iss{std::string(line)};
    RequestLine rl;
    if (!(iss >> rl.method >> rl.target >> rl.version)) return std::nullopt;
    return rl;
}

bool is_prefix_path(const fs::path& prefix, const fs::path& full) {
    auto pit = prefix.begin();
    auto fit = full.begin();
    for (; pit != prefix.end() && fit != full.end(); ++pit, ++fit) {
        if (*pit != *fit) return false;
    }
    return pit == prefix.end();
}

std::string status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

bool send_response_headers(int client_fd, int status, const std::string& content_type, std::uintmax_t content_len) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << status_text(status) << "\r\n";
    oss << "Date: " << util::now_http_date() << "\r\n";
    oss << "Server: cpp-http-file-server\r\n";
    oss << "Connection: close\r\n";
    if (!content_type.empty())
        oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << content_len << "\r\n";
    oss << "\r\n";
    auto s = oss.str();
    return util::send_all(client_fd, s.data(), s.size());
}

bool send_simple_text(int client_fd, int status, std::string_view message) {
    const std::string body = std::string(message);
    if (!send_response_headers(client_fd, status, "text/plain; charset=utf-8", body.size())) return false;
    return util::send_all(client_fd, body.data(), body.size());
}

std::string_view strip_query(std::string_view target) {
    auto q = target.find('?');
    if (q == std::string_view::npos) return target;
    return target.substr(0, q);
}

fs::path resolve_path(const fs::path& root_canon, std::string_view url_path, bool* ok) {
    *ok = false;

    std::string decoded = util::url_decode(strip_query(url_path));
    if (decoded.empty()) decoded = "/";

    // Must start with '/'
    if (decoded.front() != '/') decoded.insert(decoded.begin(), '/');

    // Basic traversal guard
    if (decoded.find('\0') != std::string::npos) return {};
    if (decoded.find("..") != std::string::npos) return {};

    // strip leading '/'
    while (!decoded.empty() && decoded.front() == '/') decoded.erase(decoded.begin());

    fs::path full = root_canon / fs::path(decoded);
    full = fs::weakly_canonical(full);

    if (!is_prefix_path(root_canon, full)) {
        return {};
    }

    *ok = true;
    return full;
}

bool stream_file(int client_fd, const fs::path& file_path) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in) return false;

    std::array<char, 64 * 1024> buf{};
    while (in) {
        in.read(buf.data(), buf.size());
        std::streamsize got = in.gcount();
        if (got > 0) {
            if (!util::send_all(client_fd, buf.data(), static_cast<std::size_t>(got))) return false;
        }
    }
    return true;
}

void handle_client(int client_fd, fs::path root_canon) {
    Fd cfd(client_fd);

    constexpr std::size_t kMaxReq = 16 * 1024;
    auto req = util::recv_http_request(cfd.get(), kMaxReq);
    if (!req) {
        // Bad / too large / disconnected
        send_simple_text(cfd.get(), 400, "Bad Request\n");
        return;
    }

    auto rl = parse_request_line(*req);
    if (!rl) {
        send_simple_text(cfd.get(), 400, "Bad Request\n");
        return;
    }

    const bool is_get = (rl->method == "GET");
    const bool is_head = (rl->method == "HEAD");
    if (!is_get && !is_head) {
        send_simple_text(cfd.get(), 405, "Method Not Allowed\n");
        return;
    }

    bool ok = false;
    fs::path full = resolve_path(root_canon, rl->target, &ok);
    if (!ok) {
        send_simple_text(cfd.get(), 403, "Forbidden\n");
        return;
    }

    // If requested a directory, try index.html
    std::error_code ec;
    if (fs::is_directory(full, ec)) {
        fs::path idx = full / "index.html";
        if (fs::exists(idx, ec) && fs::is_regular_file(idx, ec)) {
            full = idx;
        } else {
            send_simple_text(cfd.get(), 403, "Forbidden\n");
            return;
        }
    }

    if (!fs::exists(full, ec) || !fs::is_regular_file(full, ec)) {
        send_simple_text(cfd.get(), 404, "Not Found\n");
        return;
    }

    std::uintmax_t size = fs::file_size(full, ec);
    if (ec) {
        send_simple_text(cfd.get(), 500, "Internal Server Error\n");
        return;
    }

    const std::string ctype = util::mime_type_from_path(full.string());
    if (!send_response_headers(cfd.get(), 200, ctype, size)) {
        return;
    }

    if (is_head) return;

    (void)stream_file(cfd.get(), full);
}

} // namespace

HttpFileServer::HttpFileServer(uint16_t port, std::string root_dir, std::size_t threads)
    : port_(port), root_dir_(std::move(root_dir)), threads_(threads) {}

int HttpFileServer::run() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    fs::path root = root_dir_.empty() ? fs::current_path() : fs::path(root_dir_);
    root = fs::weakly_canonical(root);

    std::cout << "[http_file_server] root: " << root.string() << "\n";
    std::cout << "[http_file_server] port: " << port_ << "\n";
    std::cout << "[http_file_server] threads: " << threads_ << "\n";

    Fd server_fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd.get() < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    int yes = 1;
    if (::setsockopt(server_fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (::bind(server_fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    if (::listen(server_fd.get(), 128) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    ThreadPool pool(threads_);
    while (!g_stop.load()) {
        sockaddr_in client{};
        socklen_t len = sizeof(client);
        int cfd = ::accept(server_fd.get(), reinterpret_cast<sockaddr*>(&client), &len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
            continue;
        }

        pool.submit([cfd, root]() mutable {
            handle_client(cfd, root);
        });
    }

    std::cout << "\n[http_file_server] shutting down...\n";
    pool.stop();
    return 0;
}
