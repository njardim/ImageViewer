if (VCPKG_TARGET_IS_WINDOWS)
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()

# imageViewer overlay (D-46): minizip-ng 4.2.2 ahead of vcpkg's 4.1.0, built only with what
# OpenColorIO uses (zlib and PKWARE decryption): no bzip2, LZMA, PPMd, Zstandard, OpenSSL, WinZip AES,
# libbsd or Apple compression, so vcpkg's dependency patch for those libraries is not needed.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/zlib-ng/minizip-ng
    REF 7b2387161c542fa9f427352dcdef76097d0d692b # 4.2.2
    FETCH_REF ${VERSION}
    HEAD_REF master
)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        pkcrypt MZ_PKCRYPT
        zlib    MZ_ZLIB
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DMZ_FETCH_LIBS=OFF
        -DMZ_LIB_SUFFIX=-ng
        -DMZ_ICONV=OFF
        -DMZ_BZIP2=OFF
        -DMZ_LZMA=OFF
        -DMZ_PPMD=OFF
        -DMZ_ZSTD=OFF
        -DMZ_OPENSSL=OFF
        -DMZ_WZAES=OFF
        -DMZ_LIBBSD=OFF
        -DMZ_LIBCOMP=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_ZLIBNG=ON # minizip-ng 4.0.10 searches for zlib-ng first before zlib - we provide zlib
)

vcpkg_cmake_install()

vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/minizip-ng)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
