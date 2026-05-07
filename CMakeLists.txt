cmake_minimum_required(VERSION 3.16)

project(hardware-helper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hardware-helper
    src/main.cpp
)

target_include_directories(hardware-helper PRIVATE include)

# Linux
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(GTK REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)

    target_include_directories(hardware-helper PRIVATE
        ${GTK_INCLUDE_DIRS}
        ${WEBKIT_INCLUDE_DIRS}
    )

    target_link_libraries(hardware-helper PRIVATE
        ${GTK_LIBRARIES}
        ${WEBKIT_LIBRARIES}
    )

    target_compile_options(hardware-helper PRIVATE
        ${GTK_CFLAGS_OTHER}
        ${WEBKIT_CFLAGS_OTHER}
    )
endif()

# macOS
if(APPLE)
    target_link_libraries(hardware-helper PRIVATE
        "-framework Cocoa"
        "-framework WebKit"
    )
endif()

# Windows
if(WIN32)
    target_link_libraries(hardware-helper PRIVATE
        ole32
        comctl32
        gdi32
        user32
    )
endif()