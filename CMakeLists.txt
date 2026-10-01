cmake_minimum_required(VERSION 3.20)

project(cloudflare_tunnel
    VERSION 1.0.0
    DESCRIPTION "Production-grade C++ port and library for Cloudflare Tunnel (cloudflared)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDEs / clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Core Library: libcloudflared
# ---------------------------------------------------------------------------

set(LIB_SOURCES
    src/platform.cpp
    src/releases.cpp
    src/cache.cpp
    src/checksum.cpp
    src/downloader.cpp
    src/binary_manager.cpp
    src/output_parser.cpp
    src/process.cpp
    src/tunnel.cpp
)

add_library(cloudflared STATIC ${LIB_SOURCES})
add_library(cloudflared::cloudflared ALIAS cloudflared)

target_include_directories(cloudflared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(cloudflared PUBLIC winhttp ws2_32)
    # Enable UTF-8 source and execution character sets on MSVC
    if(MSVC)
        target_compile_options(cloudflared PUBLIC /MP /utf-8 /W4)
    endif()
elseif(ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(cloudflared PUBLIC Threads::Threads)
    target_compile_options(cloudflared PUBLIC -Wall -Wextra -Wpedantic)
else()
    find_package(CURL QUIET)
    find_package(Threads REQUIRED)
    if(CURL_FOUND)
        target_link_libraries(cloudflared PUBLIC CURL::libcurl Threads::Threads)
        target_compile_definitions(cloudflared PUBLIC HAS_LIBCURL)
    else()
        target_link_libraries(cloudflared PUBLIC Threads::Threads)
    endif()
    target_compile_options(cloudflared PUBLIC -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Code Signing for Windows Smart App Control
# ---------------------------------------------------------------------------

if(WIN32)
    function(auto_sign_target TARGET_NAME)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND powershell.exe -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sign_target.ps1" "$<TARGET_FILE:${TARGET_NAME}>"
            COMMENT "Signing ${TARGET_NAME} with developer certificate"
        )
    endfunction()
else()
    function(auto_sign_target TARGET_NAME)
    endfunction()
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
# ---------------------------------------------------------------------------
# CLI Executable: cloudflared-cli
# ---------------------------------------------------------------------------

add_executable(cloudflared_cli src/cli/main.cpp)
set_target_properties(cloudflared_cli PROPERTIES OUTPUT_NAME "cloudflared-cli")
target_link_libraries(cloudflared_cli PRIVATE cloudflared)
auto_sign_target(cloudflared_cli)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

add_executable(quick_tunnel examples/quick_tunnel.cpp)
target_link_libraries(quick_tunnel PRIVATE cloudflared)
auto_sign_target(quick_tunnel)

add_executable(url_tunnel examples/url_tunnel.cpp)
target_link_libraries(url_tunnel PRIVATE cloudflared)
auto_sign_target(url_tunnel)

add_executable(custom_options examples/custom_options.cpp)
target_link_libraries(custom_options PRIVATE cloudflared)
auto_sign_target(custom_options)

# ---------------------------------------------------------------------------
# Unit Tests
# ---------------------------------------------------------------------------

enable_testing()

function(add_cloudflared_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE cloudflared)
    auto_sign_target(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_cloudflared_test(test_platform tests/test_platform.cpp)
add_cloudflared_test(test_options tests/test_options.cpp)
add_cloudflared_test(test_checksum tests/test_checksum.cpp)
add_cloudflared_test(test_parser tests/test_parser.cpp)
add_cloudflared_test(test_binary_manager tests/test_binary_manager.cpp)
add_cloudflared_test(test_tunnel tests/test_tunnel.cpp)

endif()
