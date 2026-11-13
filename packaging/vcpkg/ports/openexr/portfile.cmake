# ImageViewer overlay (D-46, D-67, D-108, D-110): OpenEXR 3.5.3 (fixes OSV-2026-1355, within an EXR
# file's reach, and some thirty decoder defects) ahead of vcpkg's baseline 3.5.2; vcpkg's port
# (baseline 9eec002d), fetched by git at the tag's commit, which pins the source. Its one patch is
# OpenEXR's own commit that accepts OpenJPH 0.33.0 (docs/PLAN.md section 15), which goes with 3.5.4.
# Drop the overlay once vcpkg's baseline has 3.5.4 or later past the cooldown.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/AcademySoftwareFoundation/openexr
    REF 3950290530c4fd4776667d0dc4d65a9d003eb936 # v3.5.3
    FETCH_REF "v${VERSION}"
    HEAD_REF main
    PATCHES
        imageviewer-accept-newer-openjph.patch # upstream #2813 (docs/PLAN.md section 15)
)

vcpkg_check_features(OUT_FEATURE_OPTIONS OPTIONS
    FEATURES
        tools   OPENEXR_BUILD_TOOLS
        tools   OPENEXR_INSTALL_TOOLS
)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${OPTIONS}
        -DBUILD_TESTING=OFF
        -DBUILD_WEBSITE=OFF
        -DCMAKE_REQUIRE_FIND_PACKAGE_Imath=ON
        -DCMAKE_REQUIRE_FIND_PACKAGE_libdeflate=ON
        -DCMAKE_REQUIRE_FIND_PACKAGE_zstd=ON
        -DOPENEXR_BUILD_EXAMPLES=OFF
        -DOPENEXR_INSTALL_PKG_CONFIG=ON
    OPTIONS_DEBUG
        -DOPENEXR_BUILD_TOOLS=OFF
        -DOPENEXR_INSTALL_TOOLS=OFF
)
# OpenJPH is not required by CMAKE_REQUIRE_FIND_PACKAGE_openjph, as vcpkg's port has it: that makes
# the patch's first find_package(openjph 0.32.0) fatal, before its second asks for 0.33.0 (D-110).
# Its effect instead: OpenEXR must have found ours, never built the copy of OpenJPH it carries.
set(checked 0)
foreach(build_type IN ITEMS rel dbg)
    set(cache "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${build_type}/CMakeCache.txt")
    if(EXISTS "${cache}")
        file(STRINGS "${cache}" external REGEX "^OPENEXR_USE_INTERNAL_OPENJPH:INTERNAL=0$")
        if(NOT external)
            message(FATAL_ERROR "OpenEXR did not find the openjph port and would build its own copy of OpenJPH")
        endif()
        math(EXPR checked "${checked} + 1")
    endif()
endforeach()
if(checked EQUAL 0)
    message(FATAL_ERROR "no CMakeCache.txt of OpenEXR's configure found: which OpenJPH it uses is unknown")
endif()
vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/OpenEXR)

vcpkg_fixup_pkgconfig()

if(OPENEXR_INSTALL_TOOLS)
    vcpkg_copy_tools(
        TOOL_NAMES
            exr2aces
            # not installed: exrcheck
            exrenvmap
            exrheader
            exrinfo
            exrmakepreview
            exrmaketiled
            exrmanifest
            exrmetrics
            exrmultipart
            exrmultiview
            exrstdattr
        AUTO_CLEAN
    )
endif()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
