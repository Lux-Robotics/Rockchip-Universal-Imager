cmake_minimum_required(VERSION 3.25)

project(rockchip-universal-imager LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(RUI_DISABLE_GPU "Disable GPU usage in embedded browser" ON)

# Portable vs installer layout. ON: self-contained folder meant to be zipped
# and run anywhere - ships a "portable" marker so the app logs to its own
# folder, and on macOS keeps rkdeveloptool + libusb beside the .app as
# separate executables. OFF (installer): no marker (logs go to the per-user OS
# location) and on macOS the companions are sealed inside a single .app.
option(RUI_PORTABLE "Build the portable (zip-and-run) layout instead of the installer layout" OFF)

include(FetchContent)
include(CheckCXXCompilerFlag)

FetchContent_Declare(
    saucer
    GIT_REPOSITORY https://github.com/saucer/saucer
    GIT_TAG v8.0.5
)

FetchContent_MakeAvailable(saucer)

if(APPLE)
    # Idempotent substring replace: skips the edit if `replacement` (the fully
    # patched form) is already present, so re-running configure against a
    # persistent build dir (already-patched functional-src) can't re-apply a
    # replacement whose output contains its own search text (e.g. adding a
    # "typename" prefix) and double up the prefix.
    function(_patch_replace_once out_var needle replacement input)
        string(FIND "${input}" "${replacement}" already_idx)
        if(already_idx EQUAL -1)
            string(REPLACE "${needle}" "${replacement}" result "${input}")
        else()
            set(result "${input}")
        endif()
        set(${out_var} "${result}" PARENT_SCOPE)
    endfunction()

    function(patch_saucer_functional)
        set(functional_base "${CMAKE_BINARY_DIR}/_deps/functional-src/include/std23/__functional_base.h")
        set(functional_move_only "${CMAKE_BINARY_DIR}/_deps/functional-src/include/std23/move_only_function.h")

        if(EXISTS "${functional_base}")
            file(READ "${functional_base}" base_content)
            set(base_original "${base_content}")
            _patch_replace_once(base_content
                "std::invoke_result_t<decltype(_select_param_type<T>)>::type"
                "typename std::invoke_result_t<decltype(_select_param_type<T>)>::type"
                "${base_content}")
            _patch_replace_once(base_content
                "_adapt_signature<Fp>::type"
                "typename _adapt_signature<Fp>::type"
                "${base_content}")
            _patch_replace_once(base_content
                "_drop_first_arg_to_invoke<F, T>::type"
                "typename _drop_first_arg_to_invoke<F, T>::type"
                "${base_content}")
            if(NOT base_content STREQUAL base_original)
                file(WRITE "${functional_base}" "${base_content}")
            endif()
        endif()

        if(EXISTS "${functional_move_only}")
            file(READ "${functional_move_only}" mo_content)
            set(mo_original "${mo_content}")
            _patch_replace_once(mo_content
                "using Fp = quals<T>::type;"
                "using Fp = typename quals<T>::type;"
                "${mo_content}")
            _patch_replace_once(mo_content
                "using D = std::unique_ptr<T>::deleter_type;"
                "using D = typename std::unique_ptr<T>::deleter_type;"
                "${mo_content}")
            _patch_replace_once(mo_content
                "template<class T> using cv = signature::template cv<T>;"
                "template<class T> using cv = typename signature::template cv<T>;"
                "${mo_content}")
            _patch_replace_once(mo_content
                "template<class T> using ref = signature::template ref<T>;"
                "template<class T> using ref = typename signature::template ref<T>;"
                "${mo_content}")
            _patch_replace_once(mo_content
                "template<class T> using inv_quals = inv_quals_f<T>::type;"
                "template<class T> using inv_quals = typename inv_quals_f<T>::type;"
                "${mo_content}")
            _patch_replace_once(mo_content
                "using vtable = trait::vtable;"
                "using vtable = typename trait::vtable;"
                "${mo_content}")
            string(REGEX REPLACE
                "R[ \t\r\n]+operator\\(\\)\\(Args\\.\\.\\. args\\) noexcept\\(noex\\)[ \t\r\n]+requires\\(!is_const and !is_lvalue_only and[ \t\r\n]+!is_rvalue_only\\)[ \t\r\n]*\\{[^}]*\\}"
                ""
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "R[ \t\r\n]+operator\\(\\)\\(Args\\.\\.\\. args\\) const noexcept\\(noex\\)[ \t\r\n]+requires\\(is_const and !is_lvalue_only and !is_rvalue_only\\)[ \t\r\n]*\\{[^}]*\\}"
                ""
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "requires\\(!is_const and is_lvalue_only and[ \t\r\n]+!is_rvalue_only\\)"
                "requires(!is_const and !is_rvalue_only)"
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "requires\\(is_const and is_lvalue_only and[ \t\r\n]+!is_rvalue_only\\)"
                "requires(is_const and !is_rvalue_only)"
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "requires\\(!is_const and !is_lvalue_only[ \t\r\n]+and is_rvalue_only\\)"
                "requires(!is_const and !is_lvalue_only)"
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "requires\\(is_const and !is_lvalue_only and is_rvalue_only\\)"
                "requires(is_const and !is_lvalue_only)"
                mo_content
                "${mo_content}")
            if(NOT mo_content STREQUAL mo_original)
                file(WRITE "${functional_move_only}" "${mo_content}")
            endif()
        endif()
    endfunction()

    patch_saucer_functional()
endif()

FetchContent_Declare(
    reproc
    GIT_REPOSITORY https://github.com/DaanDeMeyer/reproc.git
    GIT_TAG v14.2.4
)

set(REPROC++ ON CACHE BOOL "Build reproc++" FORCE)
FetchContent_MakeAvailable(reproc)

check_cxx_compiler_flag("-Wchanges-meaning" HAS_WCHANGES_MEANING)
if(TARGET reproc++ AND HAS_WCHANGES_MEANING)
    target_compile_options(reproc++ PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-error=changes-meaning>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-changes-meaning>
    )
endif()

add_executable(rockchip-universal-imager WIN32)

# Platform-neutral sources and headers live under src/core/. Non-trivial OS
# bodies live under src/platform/{windows,posix,macos,linux}/ — prefer a
# Windows vs POSIX split when macOS and Linux share an implementation.
target_sources(rockchip-universal-imager PRIVATE
    src/main.cpp
    src/core/executable_path.cpp
    src/core/executable_path.h
    src/core/rkdeveloptool_runner.cpp
    src/core/rkdeveloptool_runner.h
    src/core/logging.cpp
    src/core/logging.h
    src/core/device_access.h
    src/core/file_dialog.h
    src/core/file_drop.h
    src/core/gpt_probe.cpp
    src/core/gpt_probe.h
    src/core/loader_map.h
    src/core/open_path.h
    src/core/quit_guard.h
    src/core/single_instance.h
    src/core/usb_monitor.h
)

if(WIN32)
    target_sources(rockchip-universal-imager PRIVATE
        src/platform/windows/device_access.cpp
        src/platform/windows/file_dialog.cpp
        src/platform/windows/file_drop.cpp
        src/platform/windows/open_path.cpp
        src/platform/windows/quit_guard.cpp
        src/platform/windows/single_instance.cpp
        src/platform/windows/usb_monitor.cpp
    )
elseif(APPLE)
    target_sources(rockchip-universal-imager PRIVATE
        src/platform/macos/device_access.cpp
        src/platform/macos/file_dialog.cpp
        src/platform/macos/file_drop.mm
        src/platform/macos/quit_guard.mm
        src/platform/macos/single_instance.mm
        src/platform/posix/open_path.cpp
        src/platform/posix/single_instance.cpp
        src/platform/posix/usb_monitor.cpp
    )

    # A real .app bundle: double-clicking a bare Unix executable in Finder
    # opens Terminal to run it; a bundle launches like a normal app (and is
    # what codesign/notarization operate on). Its runtime companions are
    # deliberately staged next to the .app, not sealed inside it.
    set_target_properties(rockchip-universal-imager PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist"
    )
else()
    target_sources(rockchip-universal-imager PRIVATE
        src/platform/linux/device_access.cpp
        src/platform/linux/file_dialog.cpp
        src/platform/posix/file_drop.cpp
        src/platform/posix/open_path.cpp
        src/platform/posix/quit_guard.cpp
        src/platform/posix/single_instance.cpp
        src/platform/posix/usb_monitor.cpp
    )
endif()

# libusb: used by the POSIX usb_monitor (hotplug). Windows uses WM_DEVICECHANGE.
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    target_include_directories(rockchip-universal-imager PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_directories(rockchip-universal-imager PRIVATE ${LIBUSB_LIBRARY_DIRS})
    target_link_libraries(rockchip-universal-imager PRIVATE ${LIBUSB_LIBRARIES})
endif()

target_link_libraries(rockchip-universal-imager PRIVATE
    saucer::saucer
    saucer::embedded
    reproc++
)

saucer_embed("${CMAKE_SOURCE_DIR}/src/ui" TARGET rockchip-universal-imager)

target_include_directories(rockchip-universal-imager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Loader .bin files are data, not code: inside the .app bundle they must go
# to Contents/Resources (codesign refuses non-Mach-O files in Contents/MacOS);
# elsewhere they sit next to the executable. resource_dir() mirrors this.
if(APPLE)
    set(RUI_LOADER_DEST "$<TARGET_FILE_DIR:rockchip-universal-imager>/../Resources/loader_binaries")
else()
    set(RUI_LOADER_DEST "$<TARGET_FILE_DIR:rockchip-universal-imager>/loader_binaries")
endif()

add_custom_command(TARGET rockchip-universal-imager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/loader_binaries
            ${RUI_LOADER_DEST}
)

if(TARGET rockchip-universal-imager)
    target_compile_options(rockchip-universal-imager PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<BOOL:${HAS_WCHANGES_MEANING}>>:-Wno-error=changes-meaning>
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<BOOL:${HAS_WCHANGES_MEANING}>>:-Wno-changes-meaning>
    )
    if(RUI_DISABLE_GPU)
        target_compile_definitions(rockchip-universal-imager PRIVATE RUI_DISABLE_GPU=1)
    endif()
endif()

if(WIN32)
    # shell32: ShellExecute (open log folder). comdlg32: open/save file dialogs.
    # setupapi: USB device enumeration; user32: message-only window +
    # RegisterDeviceNotification for native WM_DEVICECHANGE detection.
    target_link_libraries(rockchip-universal-imager PRIVATE shell32 comdlg32 setupapi user32)
    option(LIBWDI_ENABLE "Build libwdi with native MSVC sources" ON)
    set(LIBWDI_REPO "https://github.com/pbatard/libwdi.git" CACHE STRING "libwdi repository URL")
    set(LIBWDI_TAG "v1.5.0" CACHE STRING "libwdi git tag or branch")
    set(LIBWDI_LIBUSB0_DIR "" CACHE PATH "Path to libusb-win32 driver files for embedding")
    # Cross builds (x64 host -> arm64 target) set this to a directory of
    # host-built generated headers (config.h, build64.h, embedded.h). libwdi's
    # embedder/installer are build-time tools that must run on the x64 build
    # host, so a cross build can't produce embedded.h from the arm64 toolchain
    # here - it's generated by the separate host build in cmake/libwdi_host and
    # handed in through this variable. The generated headers are architecture-
    # independent (both x64 and arm64 are 64-bit; embedded.h is byte arrays),
    # so the arm64 static library below consumes them unchanged. Empty for a
    # native build, which generates them in-tree exactly as before.
    set(LIBWDI_PREBUILT_GENERATED_DIR "" CACHE PATH
        "Directory of host-built libwdi headers (config.h/build64.h/embedded.h) for cross builds")

    if(LIBWDI_ENABLE)
        if(NOT LIBWDI_LIBUSB0_DIR)
            message(FATAL_ERROR "LIBWDI_LIBUSB0_DIR not set. Point this to the libusb-win32 driver files directory.")
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

        if(LIBWDI_PREBUILT_GENERATED_DIR)
            # Cross build: reuse the host-generated headers; skip the codegen
            # tools entirely (they'd be built for arm64 and couldn't run on the
            # x64 host).
            set(LIBWDI_GENERATED_DIR "${LIBWDI_PREBUILT_GENERATED_DIR}")
            if(NOT EXISTS "${LIBWDI_GENERATED_DIR}/embedded.h")
                message(FATAL_ERROR
                    "LIBWDI_PREBUILT_GENERATED_DIR=${LIBWDI_GENERATED_DIR} has no embedded.h; "
                    "run the host libwdi build (cmake/libwdi_host) first.")
            endif()
        else()
            # Native build: generate the headers and the codegen tools in-tree.
            set(LIBWDI_GENERATED_DIR ${CMAKE_BINARY_DIR}/libwdi/generated)
            set(LIBWDI_INSTALLER_DIR ${CMAKE_BINARY_DIR}/libwdi/installer)
            file(MAKE_DIRECTORY ${LIBWDI_GENERATED_DIR})
            file(MAKE_DIRECTORY ${LIBWDI_INSTALLER_DIR})

            set(LIBWDI_LIBUSB0_DIR_CONFIG "${LIBWDI_LIBUSB0_DIR}")
            file(TO_CMAKE_PATH "${LIBWDI_LIBUSB0_DIR_CONFIG}" LIBWDI_LIBUSB0_DIR_CONFIG)

            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(LIBWDI_OPT_M64 1)
                set(LIBWDI_OPT_M32 0)
                set(LIBWDI_INSTALLER_NAME installer_x64)
                set(LIBWDI_BUILD64 1)
            else()
                set(LIBWDI_OPT_M64 0)
                set(LIBWDI_OPT_M32 1)
                set(LIBWDI_INSTALLER_NAME installer_x86)
                set(LIBWDI_BUILD64 0)
            endif()

            configure_file(
                ${CMAKE_SOURCE_DIR}/cmake/libwdi_config.h.in
                ${LIBWDI_GENERATED_DIR}/config.h
                @ONLY
            )

            configure_file(
                ${CMAKE_SOURCE_DIR}/cmake/libwdi_build64.h.in
                ${LIBWDI_GENERATED_DIR}/build64.h
                @ONLY
            )

            file(TO_NATIVE_PATH "${LIBWDI_INSTALLER_DIR}" LIBWDI_INSTALLER_DIR_NATIVE)
            string(REPLACE "\\" "\\\\" LIBWDI_INSTALLER_DIR_ESCAPED "${LIBWDI_INSTALLER_DIR_NATIVE}")

            add_executable(libwdi_installer ${LIBWDI_SRC_DIR}/installer.c)
            set_target_properties(libwdi_installer PROPERTIES OUTPUT_NAME ${LIBWDI_INSTALLER_NAME})
            set_target_properties(libwdi_installer PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${LIBWDI_INSTALLER_DIR}
                RUNTIME_OUTPUT_DIRECTORY_DEBUG ${LIBWDI_INSTALLER_DIR}
                RUNTIME_OUTPUT_DIRECTORY_RELEASE ${LIBWDI_INSTALLER_DIR}
                RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${LIBWDI_INSTALLER_DIR}
                RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${LIBWDI_INSTALLER_DIR}
            )
            target_include_directories(libwdi_installer PRIVATE
                ${LIBWDI_GENERATED_DIR}
                ${LIBWDI_SRC_DIR}
                ${libwdi_SOURCE_DIR}/msvc
            )
            target_link_libraries(libwdi_installer PRIVATE setupapi newdev ole32)

            add_executable(libwdi_embedder ${LIBWDI_SRC_DIR}/embedder.c)
            target_include_directories(libwdi_embedder PRIVATE
                ${LIBWDI_GENERATED_DIR}
                ${LIBWDI_SRC_DIR}
                ${libwdi_SOURCE_DIR}/msvc
            )
            target_compile_definitions(libwdi_embedder PRIVATE
                DDKBUILD
                SOLUTIONDIR="${LIBWDI_INSTALLER_DIR_ESCAPED}"
            )

            add_custom_command(
                OUTPUT ${LIBWDI_GENERATED_DIR}/embedded.h
                COMMAND libwdi_embedder ${LIBWDI_GENERATED_DIR}/embedded.h
                DEPENDS libwdi_embedder libwdi_installer
                WORKING_DIRECTORY ${LIBWDI_SRC_DIR}
            )
            add_custom_target(libwdi_embedded_header DEPENDS ${LIBWDI_GENERATED_DIR}/embedded.h)
        endif()

        add_library(libwdi STATIC
            ${LIBWDI_SRC_DIR}/logging.c
            ${LIBWDI_SRC_DIR}/tokenizer.c
            ${LIBWDI_SRC_DIR}/vid_data.c
            ${LIBWDI_SRC_DIR}/pki.c
            ${LIBWDI_SRC_DIR}/libwdi_dlg.c
            ${LIBWDI_SRC_DIR}/libwdi.c
            ${LIBWDI_SRC_DIR}/resource.h
            ${LIBWDI_SRC_DIR}/logging.h
            ${LIBWDI_SRC_DIR}/tokenizer.h
            ${LIBWDI_SRC_DIR}/installer.h
            ${LIBWDI_SRC_DIR}/libwdi_i.h
            ${LIBWDI_SRC_DIR}/mssign32.h
            ${LIBWDI_GENERATED_DIR}/embedded.h
        )
        if(NOT LIBWDI_PREBUILT_GENERATED_DIR)
            add_dependencies(libwdi libwdi_embedded_header)
        endif()
        target_include_directories(libwdi PUBLIC
            ${LIBWDI_GENERATED_DIR}
            ${LIBWDI_SRC_DIR}
        )
        target_include_directories(libwdi PRIVATE
            ${libwdi_SOURCE_DIR}/msvc
        )
        target_link_libraries(libwdi PUBLIC setupapi ole32 ntdll)

        set(LIBWDI_INCLUDE_DIR ${LIBWDI_SRC_DIR})
        set(LIBWDI_LIBRARY libwdi)
    else()
        find_path(LIBWDI_INCLUDE_DIR libwdi.h)
        find_library(LIBWDI_LIBRARY NAMES wdi libwdi)
    endif()

    if(LIBWDI_INCLUDE_DIR AND LIBWDI_LIBRARY)
        target_include_directories(rockchip-universal-imager PRIVATE ${LIBWDI_INCLUDE_DIR})
        target_link_libraries(rockchip-universal-imager PRIVATE ${LIBWDI_LIBRARY})
        target_compile_definitions(rockchip-universal-imager PRIVATE HAVE_LIBWDI=1)
    else()
        message(WARNING "libwdi not found; USB driver install support will be disabled")
    endif()
endif()

option(RKDEV_ENABLE "Build rkdeveloptool with ExternalProject" ON)
set(RKDEV_REPO "https://github.com/lux-robotics/rkdeveloptool.git" CACHE STRING "rkdeveloptool repository URL")
set(RKDEV_TAG "master" CACHE STRING "rkdeveloptool git tag or branch")

if(WIN32)
    set(RKDEV_BINARY_NAME "rkdeveloptool.exe" CACHE STRING "rkdeveloptool output binary name")
    set(RKDEV_BASH "C:/msys64/usr/bin/bash.exe" CACHE FILEPATH "Path to MSYS2 bash.exe for rkdeveloptool build")
    set(RKDEV_MSYSTEM "MINGW64" CACHE STRING "MSYS2 MSYSTEM for a native rkdeveloptool build")
    # Cross build (x64 host -> arm64 target). MSYS2's arm64 toolchain
    # (CLANGARM64) is arm64-native and can't run on an x64 host, so an arm64
    # rkdeveloptool is built with a pre-installed llvm-mingw cross toolchain
    # (runs on x64, targets aarch64) plus a pre-built arm64 libusb. Autotools
    # plumbing still runs under MSYS2 bash; only the compiler changes. When
    # RKDEV_CROSS_HOST is empty this whole path is inert and the native
    # MSYSTEM build above is used unchanged.
    set(RKDEV_CROSS_HOST "" CACHE STRING
        "Cross host triple for rkdeveloptool, e.g. aarch64-w64-mingw32 (empty = native build)")
    set(RKDEV_LLVM_MINGW_DIR "" CACHE PATH
        "Root of a pre-installed llvm-mingw toolchain (cross builds only)")
    set(RKDEV_CROSS_LIBUSB_PREFIX "" CACHE PATH
        "Install prefix of a pre-built arm64 libusb: lib/pkgconfig + bin/libusb-1.0.dll (cross builds only)")
else()
    set(RKDEV_BINARY_NAME "rkdeveloptool" CACHE STRING "rkdeveloptool output binary name")
endif()

set(RKDEV_PREFIX ${CMAKE_BINARY_DIR}/rkdeveloptool/install)
set(RKDEV_SOURCE_DIR ${CMAKE_BINARY_DIR}/rkdeveloptool/src)
set(RKDEV_OUTPUT_BIN ${RKDEV_PREFIX}/bin/${RKDEV_BINARY_NAME})
set(RKDEV_MSYS_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/rkdev_msys.sh)
set(RKDEV_LIBUSB_DYLIB "" CACHE FILEPATH
    "Path to the libusb dylib shipped next to the macOS app bundle")
if(APPLE AND NOT RKDEV_LIBUSB_DYLIB)
    foreach(candidate
            "/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib"
            "/usr/local/opt/libusb/lib/libusb-1.0.0.dylib")
        if(EXISTS "${candidate}")
            set(RKDEV_LIBUSB_DYLIB "${candidate}")
            break()
        endif()
    endforeach()
endif()

# The macOS distribution intentionally keeps both rkdeveloptool and libusb in
# the directory containing rockchip-universal-imager.app. Do not silently build an app
# that falls back to a developer-machine libusb installation when that
# companion dylib cannot be packaged.
if(APPLE AND RKDEV_ENABLE AND NOT EXISTS "${RKDEV_LIBUSB_DYLIB}")
    message(FATAL_ERROR
        "libusb-1.0.0.dylib was not found. Set RKDEV_LIBUSB_DYLIB to the dylib "
        "that must be shipped next to rockchip-universal-imager.app.")
endif()

if(RKDEV_ENABLE)
    include(ExternalProject)

    if(WIN32)
        if(NOT EXISTS "${RKDEV_BASH}")
            message(FATAL_ERROR "RKDEV_BASH not found. Install MSYS2 and set -DRKDEV_BASH=... or disable rkdeveloptool with -DRKDEV_ENABLE=OFF")
        endif()

        if(RKDEV_CROSS_HOST)
            # ---- arm64 cross build via llvm-mingw ----
            if(NOT RKDEV_LLVM_MINGW_DIR)
                message(FATAL_ERROR "RKDEV_CROSS_HOST is set but RKDEV_LLVM_MINGW_DIR is not. Install llvm-mingw and pass -DRKDEV_LLVM_MINGW_DIR=<path>.")
            endif()
            if(NOT RKDEV_CROSS_LIBUSB_PREFIX)
                message(FATAL_ERROR "RKDEV_CROSS_HOST is set but RKDEV_CROSS_LIBUSB_PREFIX is not. Build arm64 libusb once and pass -DRKDEV_CROSS_LIBUSB_PREFIX=<prefix>.")
            endif()
            # Prepend llvm-mingw so configure --host finds aarch64-w64-mingw32-*;
            # point pkg-config at the arm64 libusb; and tell the script where to
            # find the arm64 libusb DLL + llvm-mingw runtime DLLs to stage.
            set(RKDEV_BUILD_CMD ${RKDEV_BASH} -lc "set -e && LLVM=\$(cygpath -u '${RKDEV_LLVM_MINGW_DIR}') && LIBUSB=\$(cygpath -u '${RKDEV_CROSS_LIBUSB_PREFIX}') && export PATH=\"$LLVM/bin:/usr/bin:$PATH\" && export PKG_CONFIG_PATH=\"$LIBUSB/lib/pkgconfig\" && export PKG_CONFIG_LIBDIR=\"$LIBUSB/lib/pkgconfig\" && export RKDEV_CROSS_LIBUSB_BIN=\"$LIBUSB/bin\" && export RKDEV_CROSS_RUNTIME_DLL_DIR=\"$LLVM/${RKDEV_CROSS_HOST}/bin\" && SRC_DIR=\$(cygpath -u '<SOURCE_DIR>') && PREFIX=\$(cygpath -u '${RKDEV_PREFIX}') && SCRIPT=\$(cygpath -u '${RKDEV_MSYS_SCRIPT}') && bash \"$SCRIPT\" \"$SRC_DIR\" \"$PREFIX\" \"${RKDEV_BINARY_NAME}\" \"${RKDEV_CROSS_HOST}\"")
        else()
            # ---- native MSYSTEM build ----
            if(NOT RKDEV_MSYSTEM)
                message(FATAL_ERROR "RKDEV_MSYSTEM not set. Use -DRKDEV_MSYSTEM=MINGW64 (or CLANGARM64 on a native arm64 host)")
            endif()

            string(TOLOWER "${RKDEV_MSYSTEM}" RKDEV_MSYSTEM_LOWER)
            set(RKDEV_PATH_PREFIX "/${RKDEV_MSYSTEM_LOWER}/bin:/usr/bin")
            set(RKDEV_PKG_CONFIG_PATH "/${RKDEV_MSYSTEM_LOWER}/lib/pkgconfig:/${RKDEV_MSYSTEM_LOWER}/share/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig")
            set(RKDEV_PKG_CONFIG_LIBDIR "/${RKDEV_MSYSTEM_LOWER}/lib/pkgconfig:/${RKDEV_MSYSTEM_LOWER}/share/pkgconfig")
            set(RKDEV_BUILD_CMD ${RKDEV_BASH} -lc "export MSYSTEM=${RKDEV_MSYSTEM} && export PATH=${RKDEV_PATH_PREFIX}:$PATH && export PKG_CONFIG_PATH=${RKDEV_PKG_CONFIG_PATH} && export PKG_CONFIG_LIBDIR=${RKDEV_PKG_CONFIG_LIBDIR} && SRC_DIR=\$(cygpath -u '<SOURCE_DIR>') && PREFIX=\$(cygpath -u '${RKDEV_PREFIX}') && SCRIPT=\$(cygpath -u '${RKDEV_MSYS_SCRIPT}') && bash \"$SCRIPT\" \"$SRC_DIR\" \"$PREFIX\" \"${RKDEV_BINARY_NAME}\"")
        endif()
        set(RKDEV_CONFIGURE_CMD ${CMAKE_COMMAND} -E echo "rkdeveloptool configure handled in build step")
        set(RKDEV_INSTALL_CMD ${CMAKE_COMMAND} -E echo "rkdeveloptool install handled in build step")
    elseif(APPLE)
        # rkdeveloptool builds with autotools, which ignores
        # CMAKE_OSX_ARCHITECTURES and just targets the build host. When we're
        # asked for a single explicit arch (the CI jobs always are), forward it
        # to the compiler via -arch and put configure into cross mode with
        # --host so its feature checks don't try to *run* target-arch test
        # binaries on the build host - that's what lets an Intel runner produce
        # an arm64 rkdeveloptool.
        set(RKDEV_APPLE_ARCH_FLAGS "")
        set(RKDEV_APPLE_HOST "")
        list(LENGTH CMAKE_OSX_ARCHITECTURES _rkdev_osx_arch_count)
        if(_rkdev_osx_arch_count EQUAL 1)
            set(RKDEV_APPLE_ARCH_FLAGS "-arch ${CMAKE_OSX_ARCHITECTURES}")
            if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
                set(RKDEV_APPLE_HOST "--host=aarch64-apple-darwin")
            elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
                set(RKDEV_APPLE_HOST "--host=x86_64-apple-darwin")
            endif()
        endif()
        # Newer Apple Clang errors on VLA usage in main.cpp under rkdeveloptool's -Werror.
        # -arch goes into CC/CXX so both the compile and link steps agree.
        set(RKDEV_CONFIGURE_CMD /bin/sh -c "cd \"<SOURCE_DIR>\" && ./autogen.sh && ./configure --prefix=\"${RKDEV_PREFIX}\" ${RKDEV_APPLE_HOST} CC=\"cc ${RKDEV_APPLE_ARCH_FLAGS}\" CXX=\"c++ ${RKDEV_APPLE_ARCH_FLAGS}\" CXXFLAGS=-Wno-vla-cxx-extension")
        set(RKDEV_BUILD_CMD /bin/sh -c "cd \"<SOURCE_DIR>\" && make")
        set(RKDEV_INSTALL_CMD /bin/sh -c "cd \"<SOURCE_DIR>\" && make install")
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
        PATCH_COMMAND ${CMAKE_COMMAND} -DRKDEV_MAIN=<SOURCE_DIR>/main.cpp -P ${CMAKE_SOURCE_DIR}/cmake/rkdev_patch.cmake
        CONFIGURE_COMMAND ${RKDEV_CONFIGURE_CMD}
        BUILD_COMMAND ${RKDEV_BUILD_CMD}
        INSTALL_COMMAND ${RKDEV_INSTALL_CMD}
        BUILD_BYPRODUCTS ${RKDEV_OUTPUT_BIN}
    )

    # Tracked via add_custom_command(OUTPUT ...) + an ALL-target, rather than
    # TARGET ... POST_BUILD: POST_BUILD commands only run when rockchip-universal-imager
    # itself relinks, so an incremental build that touches no app sources
    # (only rkdeveloptool_ext, or a stale/deleted copy in build/bin) would
    # silently skip re-copying the binary. Depending on the destination path
    # as a build OUTPUT makes Ninja re-run the copy whenever it's missing.
    # rkdeveloptool and libusb ship as external companions next to the
    # bundle, so the .app does not silently carry stale fallback copies.
    set(RKDEV_DEST_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

    if(WIN32)
        set(RKDEV_DEST_BIN "${RKDEV_DEST_DIR}/${RKDEV_BINARY_NAME}")
        add_custom_command(
            OUTPUT ${RKDEV_DEST_BIN}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RKDEV_DEST_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${RKDEV_PREFIX}/bin ${RKDEV_DEST_DIR}
            DEPENDS rkdeveloptool_ext
            COMMENT "Copying rkdeveloptool to output directory"
        )
        add_custom_target(rkdeveloptool_copy ALL DEPENDS ${RKDEV_DEST_BIN})
    elseif(APPLE)
        # Two layouts (see RUI_PORTABLE):
        #  - Installer (OFF): rkdeveloptool + libusb are copied INSIDE the
        #    bundle (Contents/MacOS) so the single .app is self-contained and
        #    drag-installable. The app loads libusb from @executable_path
        #    (same dir as the app binary).
        #  - Portable (ON): they sit BESIDE the .app as separate executables,
        #    the original external-companion layout. The app loads libusb from
        #    three levels up (out of Contents/MacOS, back beside the bundle).
        if(RUI_PORTABLE)
            set(RKDEV_APP_LIBUSB_PATH "@executable_path/../../../libusb-1.0.0.dylib")
        else()
            # Contents/MacOS of the built bundle. Using a fixed path (rather
            # than a $<TARGET_FILE_DIR> genex) keeps it usable as a custom
            # command OUTPUT below.
            set(RKDEV_DEST_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rockchip-universal-imager.app/Contents/MacOS")
            set(RKDEV_APP_LIBUSB_PATH "@executable_path/libusb-1.0.0.dylib")
        endif()

        set(RKDEV_DEST_BIN "${RKDEV_DEST_DIR}/${RKDEV_BINARY_NAME}")
        set(RKDEV_COPY_OUTPUTS ${RKDEV_DEST_BIN})
        set(RKDEV_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RKDEV_DEST_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RKDEV_OUTPUT_BIN} ${RKDEV_DEST_DIR}
        )
        if(RKDEV_LIBUSB_DYLIB)
            # Keep the deployment name stable even if a caller supplies a
            # versioned/symlinked source path through RKDEV_LIBUSB_DYLIB.
            # libusb is copied right beside rkdeveloptool in both layouts, so
            # its @executable_path rpath resolves either way.
            set(RKDEV_LIBUSB_OUTPUT_NAME "libusb-1.0.0.dylib")
            list(APPEND RKDEV_COPY_OUTPUTS "${RKDEV_DEST_DIR}/${RKDEV_LIBUSB_OUTPUT_NAME}")
            list(APPEND RKDEV_COPY_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RKDEV_LIBUSB_DYLIB} ${RKDEV_DEST_DIR}/${RKDEV_LIBUSB_OUTPUT_NAME}
                COMMAND install_name_tool -add_rpath "@executable_path" ${RKDEV_DEST_BIN}
                COMMAND install_name_tool -change "/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib" "@rpath/libusb-1.0.0.dylib" ${RKDEV_DEST_BIN}
                COMMAND install_name_tool -change "/usr/local/opt/libusb/lib/libusb-1.0.0.dylib" "@rpath/libusb-1.0.0.dylib" ${RKDEV_DEST_BIN}
                COMMAND codesign --force --sign - ${RKDEV_DEST_DIR}/${RKDEV_LIBUSB_OUTPUT_NAME}
            )
        endif()
        # install_name_tool rewrites load commands in place, which invalidates
        # rkdeveloptool's existing code signature (ad-hoc or otherwise).
        # macOS on Apple Silicon requires every executable to carry a valid
        # signature to run at all, so re-sign (ad-hoc, since we have no
        # Developer ID cert) after any binary mutation or the copied tool
        # will be treated as tampered rather than merely unsigned.
        list(APPEND RKDEV_COPY_COMMANDS
            COMMAND codesign --force --sign - ${RKDEV_DEST_BIN}
        )
        add_custom_command(
            OUTPUT ${RKDEV_COPY_OUTPUTS}
            ${RKDEV_COPY_COMMANDS}
            DEPENDS rkdeveloptool_ext
            COMMENT "Copying rkdeveloptool to output directory"
        )
        add_custom_target(rkdeveloptool_copy ALL DEPENDS ${RKDEV_COPY_OUTPUTS})

        if(RUI_PORTABLE)
            # Portable: keep runtime companions external, next to
            # rockchip-universal-imager.app so they can also be used directly. Remove old
            # in-bundle copies so an incremental build cannot mask a missing
            # external dependency.
            add_custom_command(TARGET rockchip-universal-imager POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -f
                        $<TARGET_FILE_DIR:rockchip-universal-imager>/rkdeveloptool
                        $<TARGET_FILE_DIR:rockchip-universal-imager>/libusb-1.0.0.dylib
                COMMENT "Keeping macOS runtime companions outside rockchip-universal-imager.app"
            )
        endif()
        if(RKDEV_LIBUSB_DYLIB)
            add_custom_command(TARGET rockchip-universal-imager POST_BUILD
                COMMAND install_name_tool -change "/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib" "${RKDEV_APP_LIBUSB_PATH}" $<TARGET_FILE:rockchip-universal-imager>
                COMMAND install_name_tool -change "/usr/local/opt/libusb/lib/libusb-1.0.0.dylib" "${RKDEV_APP_LIBUSB_PATH}" $<TARGET_FILE:rockchip-universal-imager>
                COMMENT "Linking rockchip-universal-imager.app to its libusb"
            )
        endif()

        # Ad-hoc sign the whole bundle as the last build step, after every
        # install_name_tool/copy mutation (and the in-bundle companion copy)
        # has landed. This satisfies Apple Silicon's every-executable-must-be-
        # signed rule; it does NOT satisfy Gatekeeper for downloaded copies -
        # that needs a Developer ID plus notarization.
        add_custom_command(TARGET rockchip-universal-imager POST_BUILD
            COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:rockchip-universal-imager>
            COMMENT "Ad-hoc signing rockchip-universal-imager.app"
        )
    else()
        set(RKDEV_DEST_BIN "${RKDEV_DEST_DIR}/${RKDEV_BINARY_NAME}")
        add_custom_command(
            OUTPUT ${RKDEV_DEST_BIN}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RKDEV_DEST_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RKDEV_OUTPUT_BIN} ${RKDEV_DEST_DIR}
            DEPENDS rkdeveloptool_ext
            COMMENT "Copying rkdeveloptool to output directory"
        )
        add_custom_target(rkdeveloptool_copy ALL DEPENDS ${RKDEV_DEST_BIN})
    endif()

    add_dependencies(rockchip-universal-imager rkdeveloptool_copy)
endif()

# ============================ Packaging (CPack) ============================
# One uniform payload folder per platform; portable = that folder archived,
# installer = that folder placed in the OS's proper directory. Each CI job
# configures with its own -DRUI_PORTABLE and runs cpack with the matching
# -G generator, so a single install tree feeds exactly one package format.
#
# NOTE: this packaging is wired for CI (build.yml's 12-job matrix) and has not
# been produced/validated locally yet - treat first CI runs as the shakedown.
set(RUI_BIN_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

if(APPLE)
    # The .app carries the app binary, loaders (Contents/Resources), and - in
    # the installer layout - the companions (Contents/MacOS), all built in
    # place by the rules above, so installing the bundle captures them.
    install(TARGETS rockchip-universal-imager BUNDLE DESTINATION .)
    if(RUI_PORTABLE)
        # Portable: rkdeveloptool + libusb ship beside the .app as separate
        # executables (staged next to it in the build output).
        install(PROGRAMS "${RUI_BIN_DIR}/rkdeveloptool" DESTINATION .)
        if(RKDEV_LIBUSB_DYLIB)
            install(FILES "${RUI_BIN_DIR}/libusb-1.0.0.dylib" DESTINATION .)
        endif()
    endif()
elseif(WIN32)
    # Flat folder: app.exe + rkdeveloptool.exe + loader_binaries/. WebView2 is
    # an external runtime dependency (Evergreen), never bundled.
    install(TARGETS rockchip-universal-imager RUNTIME DESTINATION .)
    install(PROGRAMS "${RUI_BIN_DIR}/rkdeveloptool.exe" DESTINATION .)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/loader_binaries" DESTINATION .)
else()
    # Linux: portable is a flat folder (like the others); the .deb/.rpm place
    # that same folder under /opt and add a desktop entry + PATH symlink.
    # webkit2gtk/gtk/libusb are declared package dependencies, never bundled.
    if(RUI_PORTABLE)
        set(RUI_APP_DEST ".")
    else()
        set(RUI_APP_DEST "opt/rockchip-universal-imager")
    endif()
    install(TARGETS rockchip-universal-imager RUNTIME DESTINATION "${RUI_APP_DEST}")
    install(PROGRAMS "${RUI_BIN_DIR}/rkdeveloptool" DESTINATION "${RUI_APP_DEST}")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/loader_binaries" DESTINATION "${RUI_APP_DEST}")
    if(NOT RUI_PORTABLE)
        # Absolute path under the "/" package prefix (not share/... which would
        # land at /share/applications).
        install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/rockchip-universal-imager.desktop"
                DESTINATION usr/share/applications)
        # PATH launcher: /usr/bin/rockchip-universal-imager -> /opt/rockchip-universal-imager/rockchip-universal-imager
        install(CODE [[
            file(MAKE_DIRECTORY "$ENV{DESTDIR}/usr/bin")
            execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
                /opt/rockchip-universal-imager/rockchip-universal-imager
                "$ENV{DESTDIR}/usr/bin/rockchip-universal-imager")
        ]])
    endif()
endif()

# The portable "portable" marker (empty file) tells the app to log to its own
# folder (logging.cpp). Only the portable layout installs it.
if(RUI_PORTABLE)
    file(WRITE "${CMAKE_BINARY_DIR}/portable" "")
    install(FILES "${CMAKE_BINARY_DIR}/portable" DESTINATION .)
endif()

set(CPACK_PACKAGE_NAME "rockchip-universal-imager")
set(CPACK_PACKAGE_VENDOR "Rockchip Universal Imager")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Rockchip flashing and eMMC helper")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "RockchipUniversalImager")
set(CPACK_VERBATIM_VARIABLES ON)

if(APPLE)
    # DragNDrop -> .dmg; ZIP -> portable. Chosen per job via `cpack -G`.
    set(CPACK_DMG_VOLUME_NAME "Rockchip Universal Imager")
    set(CPACK_DMG_FORMAT "UDZO")
elseif(WIN32)
    # NSIS -> setup.exe (Program Files); ZIP -> portable.
    set(CPACK_NSIS_PACKAGE_NAME "Rockchip Universal Imager")
    set(CPACK_NSIS_DISPLAY_NAME "Rockchip Universal Imager")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "rockchip-universal-imager.exe")
else()
    # DEB;RPM -> installers under /opt; TGZ -> portable. The installers place
    # files at absolute paths, so anchor the package prefix at "/".
    if(NOT RUI_PORTABLE)
        set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    endif()
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Anthony Andrews")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libwebkit2gtk-4.1-0, libgtk-3-0, libadwaita-1-0, libusb-1.0-0")
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_REQUIRES "webkit2gtk4.1, gtk3, libadwaita, libusb1")
endif()

include(CPack)
