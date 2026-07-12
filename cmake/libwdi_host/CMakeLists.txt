cmake_minimum_required(VERSION 3.25)

# Host-toolchain build of libwdi's build-time codegen tools.
#
# libwdi embeds a driver installer into a generated header (embedded.h) by
# building two helper executables - installer.c and embedder.c - and RUNNING
# the embedder at build time. Those tools must execute on the build host. In a
# cross build (x64 host producing an arm64 app) the main build's toolchain
# targets arm64, so it can't build+run them. This tiny standalone project
# builds them with the HOST (x64) toolchain and emits the three generated
# headers (config.h, build64.h, embedded.h) into LIBWDI_OUTPUT_DIR, which the
# main build then consumes via -DLIBWDI_PREBUILT_GENERATED_DIR.
#
# Configure this under the host x64 MSVC environment (vcvars64). The generated
# headers are architecture-independent, so the arm64 main build reuses them.

project(libwdi_host_tools C)

set(LIBWDI_REPO "https://github.com/pbatard/libwdi.git" CACHE STRING "libwdi repository URL")
set(LIBWDI_TAG "v1.5.0" CACHE STRING "libwdi git tag or branch")
set(LIBWDI_LIBUSB0_DIR "" CACHE PATH "Path to libusb-win32 driver files for embedding")
set(LIBWDI_OUTPUT_DIR "" CACHE PATH "Where to write config.h/build64.h/embedded.h")

if(NOT LIBWDI_LIBUSB0_DIR)
    message(FATAL_ERROR "LIBWDI_LIBUSB0_DIR not set.")
endif()
if(NOT LIBWDI_OUTPUT_DIR)
    message(FATAL_ERROR "LIBWDI_OUTPUT_DIR not set.")
endif()

include(FetchContent)
FetchContent_Declare(
    libwdi
    GIT_REPOSITORY ${LIBWDI_REPO}
    GIT_TAG ${LIBWDI_TAG}
)
FetchContent_GetProperties(libwdi)
if(NOT libwdi_POPULATED)
    FetchContent_Populate(libwdi)
endif()

set(LIBWDI_SRC_DIR ${libwdi_SOURCE_DIR}/libwdi)
set(GEN ${CMAKE_BINARY_DIR}/generated)
set(INST ${CMAKE_BINARY_DIR}/installer)
file(MAKE_DIRECTORY ${GEN} ${INST} ${LIBWDI_OUTPUT_DIR})

set(LIBWDI_LIBUSB0_DIR_CONFIG "${LIBWDI_LIBUSB0_DIR}")
file(TO_CMAKE_PATH "${LIBWDI_LIBUSB0_DIR_CONFIG}" LIBWDI_LIBUSB0_DIR_CONFIG)

# The host is x64 -> 64-bit installer. The arm64 target is also 64-bit and the
# resulting embedded.h/config.h are the same either way; the embedded x64
# installer runs under Windows-on-ARM's x64 emulation on end-user machines.
set(LIBWDI_OPT_M64 1)
set(LIBWDI_OPT_M32 0)
set(LIBWDI_INSTALLER_NAME installer_x64)
set(LIBWDI_BUILD64 1)

# The template files live one directory up (the shared cmake/ folder).
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../libwdi_config.h.in ${GEN}/config.h @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../libwdi_build64.h.in ${GEN}/build64.h @ONLY)

file(TO_NATIVE_PATH "${INST}" INST_NATIVE)
string(REPLACE "\\" "\\\\" INST_ESCAPED "${INST_NATIVE}")

add_executable(libwdi_installer ${LIBWDI_SRC_DIR}/installer.c)
set_target_properties(libwdi_installer PROPERTIES OUTPUT_NAME ${LIBWDI_INSTALLER_NAME})
set_target_properties(libwdi_installer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${INST}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${INST}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${INST}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${INST}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${INST}
)
target_include_directories(libwdi_installer PRIVATE
    ${GEN}
    ${LIBWDI_SRC_DIR}
    ${libwdi_SOURCE_DIR}/msvc
)
target_link_libraries(libwdi_installer PRIVATE setupapi newdev ole32)

add_executable(libwdi_embedder ${LIBWDI_SRC_DIR}/embedder.c)
target_include_directories(libwdi_embedder PRIVATE
    ${GEN}
    ${LIBWDI_SRC_DIR}
    ${libwdi_SOURCE_DIR}/msvc
)
target_compile_definitions(libwdi_embedder PRIVATE
    DDKBUILD
    SOLUTIONDIR="${INST_ESCAPED}"
)

add_custom_command(
    OUTPUT ${GEN}/embedded.h
    COMMAND libwdi_embedder ${GEN}/embedded.h
    DEPENDS libwdi_embedder libwdi_installer
    WORKING_DIRECTORY ${LIBWDI_SRC_DIR}
)

# Publish the three generated headers to the directory the cross main build
# reads. Built by default (ALL) so a plain `cmake --build` produces them.
add_custom_target(libwdi_host_headers ALL
    DEPENDS ${GEN}/embedded.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GEN}/config.h   ${LIBWDI_OUTPUT_DIR}/config.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GEN}/build64.h  ${LIBWDI_OUTPUT_DIR}/build64.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GEN}/embedded.h ${LIBWDI_OUTPUT_DIR}/embedded.h
)
