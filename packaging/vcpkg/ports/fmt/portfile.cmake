# ImageViewer overlay (D-46, D-67, D-118): {fmt} 12.2.1 (an allocation size overflow on 32-bit
# targets, an out-of-bounds read parsing std::error_code's format specifiers, nullptr with
# FMT_COMPILE) ahead of vcpkg's baseline 12.2.0; vcpkg's port (baseline 9eec002d), fetched by git at
# the tag's commit, which pins the source. vcpkg's backport of upstream 588b3a0f (#4813, the uint128
# fallback's bitwise not) is not in 12.2.1 (a branch of 12.2.0's fixes) and stays. Drop the overlay
# once vcpkg's baseline has 12.2.1 or later past the cooldown.
vcpkg_download_distfile(FMT_BACKPORT_4813_PATCH
    URLS https://github.com/fmtlib/fmt/commit/588b3a0f8f6a8bcf2a959cae882d5b2703e86737.patch?full_index=1
    FILENAME fmt-backport-4813.patch
    SHA512 afda8fdfcdcb4b0dd5df4d4dae96a57a85fb9c4b65d0b49d51258f0913d4aed93ed146ebf96ed7b277490b1dde6c7117f43332013071441a96c3147520de8368
)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/fmtlib/fmt
    REF 208219daff58f2a7438f6931cc2ddc358acf33ef # 12.2.1
    FETCH_REF "${VERSION}"
    HEAD_REF master
    PATCHES
        "${FMT_BACKPORT_4813_PATCH}"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DFMT_CMAKE_DIR=share/fmt
        -DFMT_TEST=OFF
        -DFMT_DOC=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
