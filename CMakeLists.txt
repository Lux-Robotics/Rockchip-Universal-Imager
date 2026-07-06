cmake_minimum_required(VERSION 3.25)

project(hardware-helper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(HWHELPER_DISABLE_GPU "Disable GPU usage in embedded browser" ON)

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
                "R[[:space:]]+operator\\(Args\\.\\.\\. args\\) noexcept\\(noex\\)[[:space:]]+requires\\(!is_const and !is_lvalue_only and[[:space:]]+!is_rvalue_only\\)[[:space:]]*\\{[^}]*\\}"
                ""
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "R[[:space:]]+operator\\(Args\\.\\.\\. args\\) const noexcept\\(noex\\)[[:space:]]+requires\\(is_const and !is_lvalue_only and !is_rvalue_only\\)[[:space:]]*\\{[^}]*\\}"
                ""
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "requires\\(!is_const and is_lvalue_only and[[:space:]]+!is_rvalue_only\\)"
                "requires(!is_const and !is_rvalue_only)"
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "requires\\(is_const and is_lvalue_only and[[:space:]]+!is_rvalue_only\\)"
                "requires(is_const and !is_rvalue_only)"
                mo_content
                "${mo_content}")
            string(REGEX REPLACE
                "requires\\(!is_const and !is_lvalue_only[[:space:]]+and is_rvalue_only\\)"
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

add_executable(hardware-helper WIN32)

target_sources(hardware-helper PRIVATE
    src/main.cpp
    src/core/executable_path.cpp
    src/core/executable_path.h
    src/core/rkdeveloptool_runner.cpp
    src/core/rkdeveloptool_runner.h
    src/core/logging.cpp
    src/core/logging.h
    src/core/libusb-win32-helper.cpp
    src/core/libusb-win32-helper.h
    src/core/file_dialog.cpp
    src/core/file_dialog.h
    src/core/loader_map.h
)

target_link_libraries(hardware-helper PRIVATE
    saucer::saucer
    saucer::embedded
    reproc++
)

saucer_embed("${CMAKE_SOURCE_DIR}/src/ui" TARGET hardware-helper)

target_include_directories(hardware-helper PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_custom_command(TARGET hardware-helper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/loader_binaries
            $<TARGET_FILE_DIR:hardware-helper>/loader_binaries
)

if(TARGET hardware-helper)
    target_compile_options(hardware-helper PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<BOOL:${HAS_WCHANGES_MEANING}>>:-Wno-error=changes-meaning>
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<BOOL:${HAS_WCHANGES_MEANING}>>:-Wno-changes-meaning>
    )
    if(HWHELPER_DISABLE_GPU)
        target_compile_definitions(hardware-helper PRIVATE HWHELPER_DISABLE_GPU=1)
    endif()
endif()

if(WIN32)
    target_link_libraries(hardware-helper PRIVATE shell32)
    option(LIBWDI_ENABLE "Build libwdi with native MSVC sources" ON)
    set(LIBWDI_REPO "https://github.com/pbatard/libwdi.git" CACHE STRING "libwdi repository URL")
    set(LIBWDI_TAG "v1.5.0" CACHE STRING "libwdi git tag or branch")
    set(LIBWDI_LIBUSB0_DIR "" CACHE PATH "Path to libusb-win32 driver files for embedding")

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
        add_dependencies(libwdi libwdi_embedded_header)
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
        target_include_directories(hardware-helper PRIVATE ${LIBWDI_INCLUDE_DIR})
        target_link_libraries(hardware-helper PRIVATE ${LIBWDI_LIBRARY})
        target_compile_definitions(hardware-helper PRIVATE HAVE_LIBWDI=1)
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
    elseif(APPLE)
        # Newer Apple Clang errors on VLA usage in main.cpp under rkdeveloptool's -Werror.
        set(RKDEV_CONFIGURE_CMD /bin/sh -c "cd \"<SOURCE_DIR>\" && ./autogen.sh && ./configure --prefix=\"${RKDEV_PREFIX}\" CXXFLAGS=-Wno-vla-cxx-extension")
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
        CONFIGURE_COMMAND ${RKDEV_CONFIGURE_CMD}
        BUILD_COMMAND ${RKDEV_BUILD_CMD}
        INSTALL_COMMAND ${RKDEV_INSTALL_CMD}
        BUILD_BYPRODUCTS ${RKDEV_OUTPUT_BIN}
    )

    # Tracked via add_custom_command(OUTPUT ...) + an ALL-target, rather than
    # TARGET ... POST_BUILD: POST_BUILD commands only run when hardware-helper
    # itself relinks, so an incremental build that touches no app sources
    # (only rkdeveloptool_ext, or a stale/deleted copy in build/bin) would
    # silently skip re-copying the binary. Depending on the destination path
    # as a build OUTPUT makes Ninja re-run the copy whenever it's missing.
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
        set(RKDEV_DEST_BIN "${RKDEV_DEST_DIR}/${RKDEV_BINARY_NAME}")
        set(RKDEV_COPY_OUTPUTS ${RKDEV_DEST_BIN})
        set(RKDEV_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RKDEV_DEST_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RKDEV_OUTPUT_BIN} ${RKDEV_DEST_DIR}
        )
        if(RKDEV_LIBUSB_DYLIB)
            get_filename_component(RKDEV_LIBUSB_DYLIB_NAME "${RKDEV_LIBUSB_DYLIB}" NAME)
            list(APPEND RKDEV_COPY_OUTPUTS "${RKDEV_DEST_DIR}/${RKDEV_LIBUSB_DYLIB_NAME}")
            list(APPEND RKDEV_COPY_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RKDEV_LIBUSB_DYLIB} ${RKDEV_DEST_DIR}
                COMMAND install_name_tool -add_rpath "@executable_path" ${RKDEV_DEST_BIN}
                COMMAND install_name_tool -change "/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib" "@rpath/libusb-1.0.0.dylib" ${RKDEV_DEST_BIN}
                COMMAND install_name_tool -change "/usr/local/opt/libusb/lib/libusb-1.0.0.dylib" "@rpath/libusb-1.0.0.dylib" ${RKDEV_DEST_BIN}
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

    add_dependencies(hardware-helper rkdeveloptool_copy)
endif()