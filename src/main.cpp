#include "http_server.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

static void print_help(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [--port N] [--root DIR] [--threads N]\n"
        << "\n"
        << "A small multi-threaded HTTP/1.1 static file server.\n"
        << "\n"
        << "Options:\n"
        << "  --port N       Listening port (default: 8080)\n"
        << "  --root DIR     Root directory to serve (default: current directory)\n"
        << "  --threads N    Worker threads (default: hardware concurrency)\n"
        << "  --help         Show this help\n";
}

int main(int argc, char** argv) {
    uint16_t port = 8080;
    std::string root;
    std::size_t threads = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need_value = [&](const char* flag) {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                std::exit(2);
            }
            return std::string(argv[++i]);
        };

        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--port") {
            port = static_cast<uint16_t>(std::stoi(need_value("--port")));
        } else if (arg == "--root") {
            root = need_value("--root");
        } else if (arg == "--threads") {
            threads = static_cast<std::size_t>(std::stoul(need_value("--threads")));
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_help(argv[0]);
            return 2;
        }
    }

    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 4;
    }

    HttpFileServer server(port, root, threads);
    return server.run();
}
