# ImageViewer overlay (D-46, D-67, D-118): Little CMS 2.20, a maintenance release (a NULL dereference
# on gamut-check transforms, a double free and leaks on error paths, guards against malformed
# profiles and against too many pipeline stages, 16-bit planar premultiplied alpha, planar half-float
# formatters), ahead of vcpkg's baseline 2.19.1; vcpkg's port (baseline 9eec002d), fetched by git at
# the tag's commit, which pins the source; its shared-library patch applies unchanged. Never the
# fastfloat or threaded features (GPL-3.0, vcpkg.json). Drop the overlay once vcpkg's baseline has
# 2.20 or later past the cooldown.
if(VCPKG_LIBRARY_LINKAGE STREQUAL dynamic)
    set(SHARED_LIBRARY_PATCH "fix-shared-library.patch")
endif()

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/mm2/Little-CMS
    REF 7717fce0d85447ef3b2df0cca2e99207c15eeecd # lcms2.20
    FETCH_REF "lcms${VERSION}"
    HEAD_REF master
    PATCHES
        ${SHARED_LIBRARY_PATCH}
)

if("fastfloat" IN_LIST FEATURES)
    list(APPEND OPTIONS -Dfastfloat=true)
else()
    list(APPEND OPTIONS -Dfastfloat=false)
endif()
if("threaded" IN_LIST FEATURES)
    list(APPEND OPTIONS -Dthreaded=true)
else()
    list(APPEND OPTIONS -Dthreaded=false)
endif()
if("tools" IN_LIST FEATURES)
    list(APPEND OPTIONS -Dutils=true)
else()
    list(APPEND OPTIONS -Dutils=false)
endif()

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${OPTIONS}
)
vcpkg_install_meson()
vcpkg_fixup_pkgconfig()

if("tools" IN_LIST FEATURES)
    vcpkg_copy_tools(
        TOOL_NAMES jpgicc linkicc psicc tificc transicc tifdiff
        AUTO_CLEAN
    )
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/man")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/lcms-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/lcms2-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/lcms2")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
