cmake_minimum_required(VERSION 3.20)

project(linux_media_bar VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LMB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_executable(linux-media-bar
    src/content/content_directory.cpp
    src/main.cpp
    src/network/http/http_server.cpp
    src/network/ssdp/ssdp_server.cpp
)

target_include_directories(linux-media-bar PRIVATE src)

target_compile_options(linux-media-bar PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
)

if(LMB_ENABLE_SANITIZERS)
    target_compile_options(linux-media-bar PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(linux-media-bar PRIVATE -fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)
target_link_libraries(linux-media-bar PRIVATE Threads::Threads)

include(CTest)
