cmake_minimum_required(VERSION 3.16)

project(hardware-helper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(FetchContent)

FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG 0.12.0
)

FetchContent_MakeAvailable(webview)

add_executable(hardware-helper)

target_sources(hardware-helper PRIVATE
    src/main.cpp
)

target_link_libraries(hardware-helper PRIVATE
    webview::core
)