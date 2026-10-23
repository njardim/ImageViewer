# ImageViewer overlay (D-46, D-67, D-76): brotli 1.2.1 (security fix CVE-2026-111497, an
# out-of-bounds write in the decoder, which libjxl runs on a JPEG XL file's brob boxes) ahead of
# vcpkg's 1.2.0; vcpkg's port (baseline 0699a19d), fetched by git at the tag's commit, which pins the
# source. Drop the overlay once vcpkg's baseline has 1.2.1 or later past the cooldown.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/google/brotli
    REF db24a98aed38a16de78cc05ca15cdd8dc2a0d92d # v1.2.1
    FETCH_REF "v${VERSION}"
    HEAD_REF master
    PATCHES
        install.patch
        pkgconfig.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBROTLI_DISABLE_TESTS=ON
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(CONFIG_PATH share/unofficial-brotli PACKAGE_NAME unofficial-brotli)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/tools")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/man")

# Under emscripten the brotli executable tool is produced with .js extension but vcpkg_copy_tools
# has no special behaviour in this case and searches for the tool name with no extension
if(VCPKG_TARGET_IS_EMSCRIPTEN)
	set(TOOL_SUFFIX ".js" )
endif()

vcpkg_copy_tools(TOOL_NAMES "brotli${TOOL_SUFFIX}" SEARCH_DIR "${CURRENT_PACKAGES_DIR}/tools/brotli")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
