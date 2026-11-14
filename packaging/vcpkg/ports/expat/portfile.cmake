# ImageViewer overlay (D-46, D-110): expat 2.10.0 (security fixes CVE-2026-113351, CVE-2026-115506,
# CVE-2026-117610, CVE-2026-118370) ahead of vcpkg's 2.8.5; fetched by git at the tag's commit, which
# pins the source.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/libexpat/libexpat
    REF a70841d9ffa64ddde29ab824ed6305035ebe4c47 # R_2_10_0
    FETCH_REF R_2_10_0
    HEAD_REF master
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" EXPAT_LINKAGE)
string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "static" EXPAT_CRT_LINKAGE)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/expat"
    OPTIONS
        -DEXPAT_BUILD_EXAMPLES=OFF
        -DEXPAT_BUILD_TESTS=OFF
        -DEXPAT_BUILD_TOOLS=OFF
        -DEXPAT_BUILD_DOCS=OFF
        -DEXPAT_SHARED_LIBS=${EXPAT_LINKAGE}
        -DEXPAT_MSVC_STATIC_CRT=${EXPAT_CRT_LINKAGE}
        -DEXPAT_BUILD_PKGCONFIG=ON
    MAYBE_UNUSED_VARIABLES
        EXPAT_MSVC_STATIC_CRT
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/expat-${VERSION}")
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/doc")

vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/expat_external.h" "defined(_MSC_VER)" "defined(_WIN32)")
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/expat_external.h" "! defined(XML_STATIC)" "0")
endif()

file(COPY "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/expat/COPYING"
        "${SOURCE_PATH}/expat/lib/siphash.h"
)
