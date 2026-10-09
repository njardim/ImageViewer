# imageViewer overlay (D-38): see vcpkg.json. Derived from the vcpkg baseline port
# (434307da, 1.3.45); without delegate libraries its dependencies patch is not needed.
vcpkg_download_distfile(ARCHIVE
    URLS "https://downloads.sourceforge.net/project/graphicsmagick/graphicsmagick/${VERSION}/GraphicsMagick-${VERSION}.tar.xz"
    FILENAME "GraphicsMagick-${VERSION}.tar.xz"
    SHA512 ad721c9b57fe94a1d46a6d051156c5072d24742d974650c9200877b813d3a36d2154a8c25c40af3d06fb8f91c0fa4688905b52f49f49ea2c87d627bedb101d3a
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

set(options "")
if(VCPKG_TARGET_IS_WINDOWS)
    set(options ac_cv_header_dirent_dirent_h=no)
    # With MSVC, studio.h turns module loading on whenever the C runtime is a DLL (_DLL, which
    # /MD defines), whatever configure was told; static.c then registers none of the coders
    # built into the library and the decode worker finds no coder at all. Modules only when
    # configure builds them.
    vcpkg_replace_string("${SOURCE_PATH}/magick/studio.h"
        "#  if defined(MSWINDOWS) && defined(_DLL)"
        "#  if defined(MSWINDOWS) && defined(_DLL) && defined(BuildMagickModules)")
endif()

vcpkg_make_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    AUTORECONF
    OPTIONS
        ${options}
        # No absolute paths compiled in: an installed build looks for its configuration
        # (delegates.mgk names external programs to run) in the build machine's prefix,
        # a path that may exist, and be writable by others, on a user's computer. Not
        # installed, it looks only around the client path imageViewer's worker gives it.
        --disable-installed
        # 16 bits per sample: DICOM, MIFF, VIFF and others keep their depth.
        --with-quantum-depth=16
        --disable-openmp
        --without-modules
        --without-magick-plus-plus
        --without-perl
        # No delegate library at all: the formats they cover are decoded by imageViewer's
        # own back ends, and the decode worker allows none of their coders.
        --without-bzlib
        --without-fpx
        --without-gdi32
        --without-gs
        --without-heif
        --without-jbig
        --without-jp2
        --without-jpeg
        --without-jxl
        --without-lcms2
        --without-libzip
        --without-lzma
        --without-png
        --without-tiff
        --without-trio
        --without-ttf
        --without-webp
        --without-wmf
        --without-x
        --without-xml
        --without-zlib
        --without-zstd
)
vcpkg_make_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

# The library only: no 'gm' tool or config scripts, no configuration files (delegates.mgk
# names external programs), no documentation.
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/tools"
    "${CURRENT_PACKAGES_DIR}/lib/GraphicsMagick-${VERSION}"
    "${CURRENT_PACKAGES_DIR}/debug/lib/GraphicsMagick-${VERSION}"
    "${CURRENT_PACKAGES_DIR}/share/GraphicsMagick-${VERSION}"
    "${CURRENT_PACKAGES_DIR}/share/doc"
    "${CURRENT_PACKAGES_DIR}/share/man"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)
foreach(dir IN ITEMS "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
    file(GLOB executables "${dir}/gm${VCPKG_TARGET_EXECUTABLE_SUFFIX}" "${dir}/*-config")
    if(executables)
        file(REMOVE ${executables})
    endif()
endforeach()
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static" OR NOT VCPKG_TARGET_IS_WINDOWS)
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/Copyright.txt")
