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

FetchContent_Declare(
    reproc
    GIT_REPOSITORY https://github.com/DaanDeMeyer/reproc.git
    GIT_TAG v14.2.4
)

set(REPROC++ ON CACHE BOOL "Build reproc++" FORCE)
FetchContent_MakeAvailable(reproc)

add_executable(hardware-helper WIN32)

target_sources(hardware-helper PRIVATE
    src/main.cpp
    src/rkdeveloptool_runner.cpp
    src/rkdeveloptool_runner.h
    src/logging.cpp
    src/logging.h
    src/webview_bindings.cpp
)

target_link_libraries(hardware-helper PRIVATE
    webview::core
    reproc++
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
set(RKDEV_OUTPUT_BIN ${RKDEV_PREFIX}/bin/${RKDEV_BINARY_NAME})
set(RKDEV_MSYS_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/rkdev_msys.sh)
set(RKDEV_LIBUSB_DYLIB "")
if(APPLE)
    foreach(candidate
            "/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib"
            "/usr/local/opt/libusb/lib/libusb-1.0.0.dylib")
        if(EXISTS "${candidate}")
            set(RKDEV_LIBUSB_DYLIB "${candidate}")
            break()
        endif()
    endforeach()
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

        string(TOLOWER "${RKDEV_MSYSTEM}" RKDEV_MSYSTEM_LOWER)
        set(RKDEV_PATH_PREFIX "/${RKDEV_MSYSTEM_LOWER}/bin:/usr/bin")
        set(RKDEV_PKG_CONFIG_PATH "/${RKDEV_MSYSTEM_LOWER}/lib/pkgconfig:/${RKDEV_MSYSTEM_LOWER}/share/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig")
        set(RKDEV_PKG_CONFIG_LIBDIR "/${RKDEV_MSYSTEM_LOWER}/lib/pkgconfig:/${RKDEV_MSYSTEM_LOWER}/share/pkgconfig")
        set(RKDEV_BUILD_CMD ${RKDEV_BASH} -lc "export MSYSTEM=${RKDEV_MSYSTEM} && export PATH=${RKDEV_PATH_PREFIX}:$PATH && export PKG_CONFIG_PATH=${RKDEV_PKG_CONFIG_PATH} && export PKG_CONFIG_LIBDIR=${RKDEV_PKG_CONFIG_LIBDIR} && SRC_DIR=\$(cygpath -u '<SOURCE_DIR>') && PREFIX=\$(cygpath -u '${RKDEV_PREFIX}') && SCRIPT=\$(cygpath -u '${RKDEV_MSYS_SCRIPT}') && bash \"$SCRIPT\" \"$SRC_DIR\" \"$PREFIX\" \"${RKDEV_BINARY_NAME}\"")
        set(RKDEV_CONFIGURE_CMD ${CMAKE_COMMAND} -E echo "rkdeveloptool configure handled in build step")
        set(RKDEV_INSTALL_CMD ${CMAKE_COMMAND} -E echo "rkdeveloptool install handled in build step")
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
    )

    add_dependencies(hardware-helper rkdeveloptool_ext)

    if(WIN32)
        add_custom_command(TARGET hardware-helper POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${RKDEV_PREFIX}/bin
                    $<TARGET_FILE_DIR:hardware-helper>
        )
    elseif(APPLE)
        add_custom_command(TARGET hardware-helper POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${RKDEV_OUTPUT_BIN}
                    $<TARGET_FILE_DIR:hardware-helper>
        )
        if(RKDEV_LIBUSB_DYLIB)
            add_custom_command(TARGET hardware-helper POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${RKDEV_LIBUSB_DYLIB}
                        $<TARGET_FILE_DIR:hardware-helper>
                COMMAND install_name_tool -add_rpath "@executable_path" $<TARGET_FILE_DIR:hardware-helper>/${RKDEV_BINARY_NAME}
                COMMAND install_name_tool -change "/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib" "@rpath/libusb-1.0.0.dylib" $<TARGET_FILE_DIR:hardware-helper>/${RKDEV_BINARY_NAME}
                COMMAND install_name_tool -change "/usr/local/opt/libusb/lib/libusb-1.0.0.dylib" "@rpath/libusb-1.0.0.dylib" $<TARGET_FILE_DIR:hardware-helper>/${RKDEV_BINARY_NAME}
            )
        endif()
    else()
        add_custom_command(TARGET hardware-helper POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${RKDEV_OUTPUT_BIN}
                    $<TARGET_FILE_DIR:hardware-helper>
        )
    endif()
endif()