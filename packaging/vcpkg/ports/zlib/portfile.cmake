# ImageViewer overlay (D-46, D-67, D-69, D-118): zlib 1.4 (fixes CVE-2026-85091 in gzwrite, out of
# the packages' reach, and inflate defects within it: a write past the buffer in
# inflateGetDictionary(), a window overrun after inflateSync(), a read before the data after
# repeated inflatePrime()) ahead of vcpkg's baseline 1.3.2; vcpkg's port (baseline 9eec002d),
# fetched by git at the tag's commit, which pins the source. zlib 1.4 names its MSVC libraries
# libz and libzs (its ChangeLog: "Change CMake library names on MSVC"), so the pkg-config file, the
# CMake wrapper and the package's component checks follow them, as vcpkg's own 1.4 recipe does;
# its OHOS patch is vcpkg's, rebased on 1.4. Drop the overlay once vcpkg's baseline has 1.4 or
# later past the cooldown.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/madler/zlib
    REF 99623ef7097352da7993ba614161fd6a311e046e # v1.4
    FETCH_REF "v${VERSION}"
    HEAD_REF master
    PATCHES
        0001-no-version-script-on-ohos.patch
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" ZLIB_BUILD_SHARED)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" ZLIB_BUILD_STATIC)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DZLIB_BUILD_TESTING=OFF
        -DZLIB_BUILD_SHARED=${ZLIB_BUILD_SHARED}
        -DZLIB_BUILD_STATIC=${ZLIB_BUILD_STATIC}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/zlib")
vcpkg_fixup_pkgconfig()

# The library's name in zlib.pc: z (libz.so, libz.dylib, MinGW's libz.dll.a); with MSVC libz.lib, or
# libzs.lib when static, and a "d" for the debug build.
if(VCPKG_TARGET_IS_WINDOWS)
    set(prefix "")
    if(NOT VCPKG_TARGET_IS_MINGW)
        set(prefix "lib")
    endif()
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc" " -lz" " -l${prefix}zs")
    else()
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc" " -lz" " -l${prefix}z")
    endif()
    if(NOT VCPKG_BUILD_TYPE)
        if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
            vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc" " -lz" " -l${prefix}zsd")
        else()
            vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc" " -lz" " -l${prefix}zd")
        endif()
    endif()
endif()

# One of the two libraries is built: the package's config, which without components asks for both
# targets, asks only for that one.
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/zconf.h" "ifdef ZLIB_DLL" "if 0")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/zlib/ZLIBConfig.cmake" [[_ZLIB_supported_components "shared" "static"]] [[_ZLIB_supported_components "static"]])
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/zlib/ZLIBConfig.cmake" [[if(NOT TARGET ZLIB::ZLIB)]] [[if(FALSE)]])
else()
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/zconf.h" "ifdef ZLIB_DLL" "if 1")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/zlib/ZLIBConfig.cmake" [[_ZLIB_supported_components "shared" "static"]] [[_ZLIB_supported_components "shared"]])
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/zlib/ZLIBConfig.cmake" [[if(NOT TARGET ZLIB::ZLIBSTATIC)]] [[if(FALSE)]])
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
