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

add_executable(hardware-helper WIN32)

target_sources(hardware-helper PRIVATE
    src/main.cpp
)

target_link_libraries(hardware-helper PRIVATE
    webview::core
)

option(RKDEV_ENABLE "Build rkdeveloptool with ExternalProject" ON)
set(RKDEV_REPO "https://github.com/lux-robotics/rkdeveloptool.git" CACHE STRING "rkdeveloptool repository URL")
set(RKDEV_TAG "master" CACHE STRING "rkdeveloptool git tag or branch")

if(WIN32)
    set(RKDEV_BINARY_NAME "rkdeveloptool.exe" CACHE STRING "rkdeveloptool output binary name")
    set(RKDEV_BASH "C:/msys64/usr/bin/bash.exe" CACHE FILEPATH "Path to MSYS2 bash.exe for rkdeveloptool build")
    set(RKDEV_MSYSTEM "MINGW64" CACHE STRING "MSYS2 MSYSTEM for rkdeveloptool build")
else()
    set(RKDEV_BINARY_NAME "rkdeveloptool" CACHE STRING "rkdeveloptool output binary name")
endif()

set(RKDEV_PREFIX ${CMAKE_BINARY_DIR}/rkdeveloptool/install)
set(RKDEV_SOURCE_DIR ${CMAKE_BINARY_DIR}/rkdeveloptool/src)
set(RKDEV_BUILT_BIN ${RKDEV_SOURCE_DIR}/${RKDEV_BINARY_NAME})
if(WIN32)
    set(RKDEV_OUTPUT_BIN ${RKDEV_BUILT_BIN})
else()
    set(RKDEV_OUTPUT_BIN ${RKDEV_PREFIX}/bin/${RKDEV_BINARY_NAME})
endif()

if(RKDEV_ENABLE)
    include(ExternalProject)

    if(WIN32)
        if(NOT EXISTS "${RKDEV_BASH}")
            message(FATAL_ERROR "RKDEV_BASH not found. Install MSYS2 and set -DRKDEV_BASH=... or disable rkdeveloptool with -DRKDEV_ENABLE=OFF")
        endif()

        if(NOT RKDEV_MSYSTEM)
            message(FATAL_ERROR "RKDEV_MSYSTEM not set. Use -DRKDEV_MSYSTEM=MINGW64 or -DRKDEV_MSYSTEM=CLANGARM64")
        endif()

        file(TO_CMAKE_PATH "${RKDEV_PREFIX}" RKDEV_PREFIX_POSIX)
        string(TOLOWER "${RKDEV_MSYSTEM}" RKDEV_MSYSTEM_LOWER)
        set(RKDEV_PATH_PREFIX "/${RKDEV_MSYSTEM_LOWER}/bin:/usr/bin")
        set(RKDEV_CONFIGURE_CMD ${RKDEV_BASH} -lc "export MSYSTEM=${RKDEV_MSYSTEM}; export PATH=${RKDEV_PATH_PREFIX}:$PATH; cd \"<SOURCE_DIR>\" && ./autogen.sh && ./configure --prefix=\"${RKDEV_PREFIX_POSIX}\"")
        set(RKDEV_BUILD_CMD ${RKDEV_BASH} -lc "export MSYSTEM=${RKDEV_MSYSTEM}; export PATH=${RKDEV_PATH_PREFIX}:$PATH; cd \"<SOURCE_DIR>\" && make")
        set(RKDEV_INSTALL_CMD "")
    else()
        set(RKDEV_CONFIGURE_CMD /bin/sh -c "cd \"<SOURCE_DIR>\" && ./autogen.sh && ./configure --prefix=\"${RKDEV_PREFIX}\"")
        set(RKDEV_BUILD_CMD /bin/sh -c "cd \"<SOURCE_DIR>\" && make")
        set(RKDEV_INSTALL_CMD /bin/sh -c "cd \"<SOURCE_DIR>\" && make install")
    endif()

    ExternalProject_Add(rkdeveloptool_ext
        GIT_REPOSITORY ${RKDEV_REPO}
        GIT_TAG ${RKDEV_TAG}
        UPDATE_COMMAND ""
        SOURCE_DIR ${RKDEV_SOURCE_DIR}
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ${RKDEV_CONFIGURE_CMD}
        BUILD_COMMAND ${RKDEV_BUILD_CMD}
        INSTALL_COMMAND ${RKDEV_INSTALL_CMD}
        BUILD_BYPRODUCTS ${RKDEV_OUTPUT_BIN}
    )

    add_dependencies(hardware-helper rkdeveloptool_ext)

    add_custom_command(TARGET hardware-helper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${RKDEV_OUTPUT_BIN}
                $<TARGET_FILE_DIR:hardware-helper>
    )
endif()