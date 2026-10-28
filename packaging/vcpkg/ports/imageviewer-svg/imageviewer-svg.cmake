# Builds and installs ImageViewer's SVG renderer (D-90): the crate beside this file (our contract,
# imageviewer_svg.h, implemented with resvg) inside resvg's source tree at the pinned tag. One
# definition for both builds: portfile.cmake includes it (vcpkg, CI), and scripts/build-svg.sh runs it
# as a script for local builds:
#   cmake -DSOURCE_DIR=<resvg checkout> -DBUILD_DIR=<dir> -DPREFIX=<prefix> -P imageviewer-svg.cmake
#
# Installs <prefix>/lib/<static library>, <prefix>/include/imageviewer_svg.h, the CMake package
# <prefix>/share/imageviewer-svg/imageviewer-svg-config.cmake (target imageviewer::svg), and in the
# same directory crates.tsv (every crate built in: name, version, licence, source) and
# licences.txt (their licence texts), read by the licence gate and shipped with the packages.

cmake_policy(SET CMP0057 NEW) # if(... IN_LIST ...), in script mode too

# The Rust target triple of this build: vcpkg's triplet, or the machine itself for a local build
# (script mode knows no target platform, only the host).
function(imageviewer_svg_rust_target out)
    if(DEFINED VCPKG_TARGET_ARCHITECTURE)
        set(architecture "${VCPKG_TARGET_ARCHITECTURE}")
    else()
        cmake_host_system_information(RESULT architecture QUERY OS_PLATFORM)
    endif()
    if((VCPKG_TARGET_IS_WINDOWS OR (NOT DEFINED VCPKG_TARGET_ARCHITECTURE AND CMAKE_HOST_WIN32))
       AND architecture MATCHES "^(x64|AMD64)$")
        set(triple x86_64-pc-windows-msvc)
    elseif((VCPKG_TARGET_IS_OSX OR (NOT DEFINED VCPKG_TARGET_ARCHITECTURE AND CMAKE_HOST_APPLE))
           AND architecture MATCHES "^(arm64)$")
        set(triple aarch64-apple-darwin)
    elseif((VCPKG_TARGET_IS_LINUX OR (NOT DEFINED VCPKG_TARGET_ARCHITECTURE AND CMAKE_HOST_UNIX AND NOT CMAKE_HOST_APPLE))
           AND architecture MATCHES "^(x64|x86_64)$")
        set(triple x86_64-unknown-linux-gnu)
    else()
        message(FATAL_ERROR "imageviewer-svg: no Rust target for this system (${architecture})")
    endif()
    set(${out} ${triple} PARENT_SCOPE)
endfunction()

# SOURCE_DIR: resvg's tree at the tag; BUILD_DIR: cargo's own directories; PREFIX: where to
# install; CARGO: cargo (a rustup proxy, which takes the toolchain of rust-toolchain.toml).
function(imageviewer_svg_build SOURCE_DIR BUILD_DIR PREFIX CARGO)
    set(crate "${SOURCE_DIR}/imageviewer-svg")
    file(MAKE_DIRECTORY "${crate}")
    foreach(name Cargo.toml Cargo.lock lib.rs rust-toolchain.toml)
        file(COPY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${name}" DESTINATION "${crate}")
    endforeach()
    imageviewer_svg_rust_target(target)

    # A private cargo home: no user configuration (a source replacement, a [patch]) reaches the
    # build, and the crates are fetched from crates.io and checked against Cargo.lock's checksums.
    set(ENV{CARGO_HOME} "${BUILD_DIR}/cargo-home")
    set(ENV{CARGO_TARGET_DIR} "${BUILD_DIR}/target")
    if(DEFINED VCPKG_OSX_DEPLOYMENT_TARGET)
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${VCPKG_OSX_DEPLOYMENT_TARGET}")
    endif()
    # --print native-static-libs: the system libraries the static library needs at link time.
    execute_process(
        COMMAND "${CARGO}" rustc --locked --release --target ${target} -- --print native-static-libs
        WORKING_DIRECTORY "${crate}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    file(WRITE "${BUILD_DIR}/cargo-build.log" "${out}\n${err}")
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "imageviewer-svg: cargo failed (${status}); see ${BUILD_DIR}/cargo-build.log\n${err}")
    endif()
    if(NOT err MATCHES "native-static-libs: ([^\r\n]*)")
        message(FATAL_ERROR "imageviewer-svg: cargo did not list the native libraries")
    endif()
    separate_arguments(flags UNIX_COMMAND "${CMAKE_MATCH_1}")
    set(native "")
    set(framework OFF)
    foreach(flag IN LISTS flags)
        if(framework)
            list(APPEND native "-framework ${flag}")
            set(framework OFF)
        elseif(flag STREQUAL "-framework")
            set(framework ON)
        elseif(flag MATCHES "^-l(.+)$")
            list(APPEND native "${CMAKE_MATCH_1}")
        elseif(flag MATCHES "^([A-Za-z0-9_]+)\\.lib$")
            list(APPEND native "${CMAKE_MATCH_1}")
        elseif(NOT flag MATCHES "^/defaultlib:") # the C runtime: the application links its own
            message(FATAL_ERROR "imageviewer-svg: unexpected link flag '${flag}'")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES native)

    if(target MATCHES "windows-msvc")
        set(library imageviewer_svg.lib)
    else()
        set(library libimageviewer_svg.a)
    endif()
    file(INSTALL "${BUILD_DIR}/target/${target}/release/${library}" DESTINATION "${PREFIX}/lib")
    file(INSTALL "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/imageviewer_svg.h" DESTINATION "${PREFIX}/include")
    set(share "${PREFIX}/share/imageviewer-svg")
    file(READ "${crate}/Cargo.toml" manifest)
    string(REGEX MATCH "\nversion = \"([^\"]+)\"" _ "${manifest}")
    set(IV_SVG_VERSION "${CMAKE_MATCH_1}")
    set(IV_SVG_LIBRARY "${library}")
    set(IV_SVG_NATIVE_LIBS "${native}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/imageviewer-svg-config.cmake.in"
                   "${share}/imageviewer-svg-config.cmake" @ONLY)

    # Every crate built into the library, for this target, and its licence texts.
    execute_process(
        COMMAND "${CARGO}" metadata --locked --offline --format-version 1 --filter-platform ${target}
        WORKING_DIRECTORY "${crate}"
        RESULT_VARIABLE status OUTPUT_VARIABLE metadata ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "imageviewer-svg: cargo metadata failed: ${err}")
    endif()
    string(JSON node_count LENGTH "${metadata}" resolve nodes)
    set(built "")
    math(EXPR last "${node_count} - 1")
    foreach(i RANGE ${last})
        string(JSON id GET "${metadata}" resolve nodes ${i} id)
        list(APPEND built "${id}")
    endforeach()
    string(JSON package_count LENGTH "${metadata}" packages)
    math(EXPR last "${package_count} - 1")
    set(table "")
    set(texts "")
    foreach(i RANGE ${last})
        string(JSON id GET "${metadata}" packages ${i} id)
        if(NOT id IN_LIST built)
            continue()
        endif()
        string(JSON name GET "${metadata}" packages ${i} name)
        string(JSON version GET "${metadata}" packages ${i} version)
        string(JSON licence GET "${metadata}" packages ${i} license)
        string(JSON source GET "${metadata}" packages ${i} source)
        string(JSON manifest_path GET "${metadata}" packages ${i} manifest_path)
        if(name STREQUAL "imageviewer-svg")
            continue() # ImageViewer's own code, under the LICENSE every package carries
        endif()
        get_filename_component(directory "${manifest_path}" DIRECTORY)
        if(source STREQUAL "" OR source STREQUAL "null")
            set(source "resvg ${IV_SVG_VERSION}") # resvg's own crates: their licence is at the tree's root
            set(directory "${SOURCE_DIR}")
        else()
            set(source "crates.io")
        endif()
        string(APPEND table "${name}\t${version}\t${licence}\t${source}\n")
        file(GLOB notices LIST_DIRECTORIES false "${directory}/LICENSE*" "${directory}/LICENCE*" "${directory}/COPYING*"
             "${directory}/UNLICENSE*" "${directory}/NOTICE*")
        if(NOT notices)
            message(FATAL_ERROR "imageviewer-svg: ${name} ${version} has no licence text in ${directory}")
        endif()
        list(SORT notices)
        string(APPEND texts "==== ${name} ${version} (${licence}) ====\n\n")
        foreach(notice IN LISTS notices)
            file(READ "${notice}" body)
            get_filename_component(file_name "${notice}" NAME)
            string(APPEND texts "---- ${file_name} ----\n${body}\n")
        endforeach()
    endforeach()
    file(WRITE "${share}/crates.tsv" "${table}")
    file(WRITE "${share}/licences.txt"
         "ImageViewer's SVG renderer: resvg ${IV_SVG_VERSION} and the crates built into it.\n\n${texts}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE) # cmake -P, a local build
    foreach(variable SOURCE_DIR BUILD_DIR PREFIX)
        if(NOT ${variable})
            message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<resvg checkout> -DBUILD_DIR=<dir> -DPREFIX=<prefix> -P ${CMAKE_CURRENT_LIST_FILE}")
        endif()
    endforeach()
    find_program(CARGO NAMES cargo PATHS "$ENV{HOME}/.cargo/bin" REQUIRED)
    imageviewer_svg_build("${SOURCE_DIR}" "${BUILD_DIR}" "${PREFIX}" "${CARGO}")
endif()
