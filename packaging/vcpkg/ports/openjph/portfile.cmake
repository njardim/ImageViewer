# ImageViewer overlay (D-46, D-67, D-73): vcpkg's port (baseline 0699a19d), fetched by git at the
# tag's commit, so the commit hash pins the source and scripts/update_dependencies.py moves it to each
# release once it is past the cooldown. It exists for our patches (imageviewer-*, docs/PLAN.md
# section 15): drop the overlay once they are all gone and vcpkg has the version.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/aous72/OpenJPH
    REF 23c422895ce6c3a156935222e4715ee0b7be952c # 0.32.0
    FETCH_REF "${VERSION}"
    HEAD_REF master
    PATCHES
        xsi-strerror_r.patch
        fix-pkgconfig-library-name.patch
        fix-msvc-x86-ice.patch
        imageviewer-codeblock-row-pairs.patch # ours: CVE-2026-111151, no upstream fix yet
        imageviewer-nlt-marker-points.patch # upstream b7b2dee, after 0.32.0
        imageviewer-dfs-index-null.patch # upstream 287f8b1, after 0.32.0
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
