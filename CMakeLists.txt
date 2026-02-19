cmake_minimum_required(VERSION 3.16)
project(http_file_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(http_file_server
    src/main.cpp
    src/thread_pool.cpp
    src/http_server.cpp
    src/util.cpp
)

target_include_directories(http_file_server PRIVATE include)

if(UNIX)
  target_link_libraries(http_file_server PRIVATE pthread)
endif()

# GCC < 9 sometimes needs explicit stdc++fs for <filesystem>
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(http_file_server PRIVATE stdc++fs)
  endif()
endif()
