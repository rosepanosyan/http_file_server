#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace util {

std::string now_http_date();

bool send_all(int fd, const void* data, std::size_t size);

std::optional<std::string> recv_http_request(int fd, std::size_t max_bytes);

std::string url_decode(std::string_view s);

std::string mime_type_from_path(const std::string& path);

std::string trim(std::string s);

} // namespace util
