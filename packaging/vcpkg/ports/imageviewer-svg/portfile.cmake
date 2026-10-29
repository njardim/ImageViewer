# ImageViewer overlay (D-80, D-90): ImageViewer's SVG renderer, the contract imageviewer_svg.h
# implemented with resvg. resvg is fetched by git at the tag's commit (scripts/update_dependencies.py
# moves it to each release once it is past the cooldown, with the crate's version and Cargo.lock);
# the crate beside this file is built inside that tree by imageviewer-svg.cmake, with the Rust of
# rust-toolchain.toml. A static library: nothing to deploy, and no Rust runtime beside the program.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
set(VCPKG_BUILD_TYPE release) # the application is built as Release only

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/linebender/resvg"
    REF 68b14c4c3bccdb60344c777406486b54c36ec1a4 # v0.48.1
    FETCH_REF "v${VERSION}"
    HEAD_REF main
)

# rustup's cargo: CI installs the pinned toolchain (build.yml) and keeps CARGO_HOME and RUSTUP_HOME
# in vcpkg's build environment (VCPKG_KEEP_ENV_VARS, which Windows needs).
find_program(CARGO NAMES cargo PATHS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin"
             NO_DEFAULT_PATH)
find_program(CARGO NAMES cargo REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/imageviewer-svg.cmake")
set(build_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
file(REMOVE_RECURSE "${build_dir}")
imageviewer_svg_build("${SOURCE_PATH}" "${build_dir}" "${CURRENT_PACKAGES_DIR}" "${CARGO}")

# The licence texts of resvg and every crate built in, as vcpkg's copyright file: CMakeLists.txt ships
# share/*/copyright with the packages.
file(RENAME "${CURRENT_PACKAGES_DIR}/share/${PORT}/licences.txt" "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright")
