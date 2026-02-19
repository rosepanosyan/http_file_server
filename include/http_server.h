#pragma once

#include <cstdint>
#include <string>

class ThreadPool;

/**
 * Very small HTTP/1.1 static file server.
 *
 * Supported:
 *  - GET /path and HEAD /path
 *  - directory traversal protection
 *  - MIME type detection by extension
 *
 * Note: one request per connection (Connection: close).
 */
class HttpFileServer {
public:
    HttpFileServer(uint16_t port, std::string root_dir, std::size_t threads);

    // Blocking call: starts listening and serving until SIGINT/SIGTERM or fatal error.
    int run();

private:
    uint16_t port_;
    std::string root_dir_;
    std::size_t threads_;
};
