# http_file_server

A small **multi-threaded HTTP/1.1 static file server** implemented in modern **C++17** using **POSIX sockets** and a custom **thread pool**.


A small **multi-threaded HTTP/1.1 static file server** implemented in modern **C++17** using **POSIX sockets** and a custom **thread pool**.

## What it demonstrates
- Linux networking basics: `socket/bind/listen/accept`, `send/recv`
- Concurrency: thread pool that handles connections in parallel
- Security basics: directory traversal protection
- Clean code: RAII, error handling, CMake build

## Build
```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j
```

## Run
Serve the current directory on port 8080:
```bash
./http_file_server --port 8080 --root . --threads 4
```

Then open:
- http://localhost:8080/

## Notes
- Supports **GET** and **HEAD**
- One request per connection (`Connection: close`)

