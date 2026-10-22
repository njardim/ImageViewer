# ImageViewer overlay (D-46, D-67, D-73): vcpkg's port (baseline 0699a19d), fetched by git at the
# tag's commit, so the commit hash pins the source and scripts/update_dependencies.py moves it to each
# release once it is past the cooldown. It exists for our patch (imageviewer-*, docs/PLAN.md
# section 15): drop the overlay once it is gone and vcpkg has the version.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/uclouvain/openjpeg
    REF 6c4a29b00211eb0430fa0e5e890f1ce5c80f409f # v2.5.4
    FETCH_REF "v${VERSION}"
    HEAD_REF master
    PATCHES
        pkgconfig.diff
        third-party.diff
        imageviewer-read-sod-index.patch # upstream 91d08b11 (OSV-2025-219), in no release yet
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" BUILD_STATIC_LIBS)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        "jpip"          BUILD_JPIP
        "tools"         BUILD_CODEC
        "tools"         BUILD_LUTS_GENERATOR
)

if(NOT VCPKG_TARGET_IS_WINDOWS AND "tools" IN_LIST FEATURES)
    list(APPEND FEATURE_OPTIONS 
        -DBUILD_JPIP_SERVER=ON
        "-DFCGI_INCLUDE_DIR=${CURRENT_INSTALLED_DIR}/include/fastcgi"
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_DOC:BOOL=OFF
        -DBUILD_STATIC_LIBS=${BUILD_STATIC_LIBS}
        -DCMAKE_DISABLE_FIND_PACKAGE_Java=ON
        -DOPENJPEG_INSTALL_SUBDIR=.
        -DOPENJPEG_INSTALL_PACKAGE_DIR=share/openjpeg
        ${FEATURE_OPTIONS}
    MAYBE_UNUSED_VARIABLES
        CMAKE_DISABLE_FIND_PACKAGE_Java
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup()
vcpkg_fixup_pkgconfig()

set(TOOL_NAMES "")
if("tools" IN_LIST FEATURES)
    list(APPEND TOOL_NAMES opj_compress opj_decompress opj_dump opj_dec_server opj_jpip_addxml opj_jpip_test opj_jpip_transcode)
endif()
if(TOOL_NAMES)
    vcpkg_copy_tools(TOOL_NAMES ${TOOL_NAMES} AUTO_CLEAN)
endif()

if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    file(WRITE "${CURRENT_PACKAGES_DIR}/include/openjpeg\.h" [[
/* vcpkg VS legacy compatibility */
#include "openjpeg-2.5/openjpeg.h"
]])
    file(WRITE "${CURRENT_PACKAGES_DIR}/include/opj_config\.h" [[
/* vcpkg VS legacy compatibility */
#include "openjpeg-2.5/opj_config.h"
]])
endif()

file(READ "${CURRENT_PACKAGES_DIR}/include/openjpeg-2.5/openjpeg\.h" OPENJPEG_H)
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    string(REPLACE "defined(OPJ_STATIC)" "1" OPENJPEG_H "${OPENJPEG_H}")
else()
    string(REPLACE "defined(OPJ_STATIC)" "0" OPENJPEG_H "${OPENJPEG_H}")
endif()
string(REPLACE "defined(DLL_EXPORT)" "0" OPENJPEG_H "${OPENJPEG_H}")
file(WRITE "${CURRENT_PACKAGES_DIR}/include/openjpeg-2.5/openjpeg\.h" "${OPENJPEG_H}")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
