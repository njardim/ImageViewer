# ImageViewer overlay (D-46, D-67, D-73, D-110): vcpkg's port (baseline 0699a19d), fetched by git at
# the tag's commit, so the commit hash pins the source and scripts/update_dependencies.py moves it to
# each release once it is past the cooldown. OpenJPH 0.33.0 (fixes OSV-2026-1371 and OSV-2026-1517,
# and holds the fixes our three patches carried) ahead of vcpkg's 0.32.0; vcpkg's own patches, but
# fix-msvc-x86-ice, which 0.33.0 holds. Drop the overlay once vcpkg's baseline has 0.33.0 or later.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/aous72/OpenJPH
    REF 345a8bcd1c7a3908e3870797057277660a412073 # 0.33.0
    FETCH_REF "${VERSION}"
    HEAD_REF master
    PATCHES
        xsi-strerror_r.patch
        fix-pkgconfig-library-name.patch
)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        tools OJPH_BUILD_EXECUTABLES
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DOJPH_ENABLE_TIFF_SUPPORT=ON
        -DOJPH_BUILD_TESTS=OFF
        -DOJPH_BUILD_STREAM_EXPAND=ON
        ${FEATURE_OPTIONS}
    OPTIONS_DEBUG
        -DOJPH_BUILD_EXECUTABLES=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/openjph)

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

if("tools" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES ojph_expand ojph_compress ojph_stream_expand AUTO_CLEAN)
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
