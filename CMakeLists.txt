cmake_minimum_required(VERSION 3.24)

# Versions are X.Y with an optional suffix: 0.1, 0.2-alpha, 1.0 (decision D-19).
# Release builds take it from the tag (CI passes -DIMAGEVIEWER_VERSION=...);
# IMAGEVIEWER_BUILD (e.g. the commit) is appended for development builds.
set(IMAGEVIEWER_VERSION "0.2-dev" CACHE STRING "Version: X.Y or X.Y-suffix")
set(IMAGEVIEWER_BUILD "" CACHE STRING "Build label appended as +label (empty for releases)")
if(NOT IMAGEVIEWER_VERSION MATCHES "^([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.]+)?$")
    message(FATAL_ERROR "IMAGEVIEWER_VERSION must be X.Y or X.Y-suffix, got '${IMAGEVIEWER_VERSION}'")
endif()
project(imageviewer VERSION ${CMAKE_MATCH_1}.${CMAKE_MATCH_2} LANGUAGES CXX)
set(IMAGEVIEWER_VERSION_STRING "${IMAGEVIEWER_VERSION}")
if(IMAGEVIEWER_BUILD)
    string(APPEND IMAGEVIEWER_VERSION_STRING "+${IMAGEVIEWER_BUILD}")
endif()
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6.11: QRhi (HDR swapchains) is reached through Qt6::GuiPrivate; its API is
# stable enough for us but carries no compatibility promise (pin Qt minor versions).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets Concurrent ShaderTools LinguistTools)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.12)
    message(FATAL_ERROR "Qt ${Qt6_VERSION}: the private QRhi API used here is validated on Qt 6.11 only; "
                        "moving to a new Qt minor version is a deliberate port (docs/PLAN.md).")
endif()
# User-interface languages (decisions D-27, D-32): English is the source; the 16 most spoken
# languages, Korean, Italian, Turkish and every official EU language. Keep in sync with
# uiLanguages() in src/settings.cpp.
qt_standard_project_setup(
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES
        zh_CN hi es ar fr bn pt id ur ru de ja mr vi te
        ko it tr
        bg hr cs da nl et fi el hu ga lv lt mt pl ro sk sl sv
)

find_package(OpenImageIO CONFIG REQUIRED)

# Little CMS: CMake config from vcpkg, pkg-config elsewhere (e.g. Linux distributions).
find_package(lcms2 CONFIG QUIET)
if(NOT TARGET lcms2::lcms2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
    add_library(lcms2::lcms2 ALIAS PkgConfig::LCMS2)
endif()

qt_add_executable(imageviewer
    src/main.cpp
    src/viewer.h src/viewer.cpp src/navigation.cpp src/overlays.cpp src/commands.cpp
    src/cache.h src/cache.cpp
    src/settings.h src/settings.cpp
    src/renderer.h src/renderer.cpp
    src/image.h src/image.cpp
    src/color.h src/color.cpp
    src/folder.h src/folder.cpp
)

qt_add_shaders(imageviewer "shaders"
    PREFIX "/shaders"
    BASE src/shaders
    FILES
        src/shaders/image.vert
        src/shaders/image.frag
)

# translations/imageviewer_<lang>.ts, compiled into the executable under :/i18n.
# Refresh them after changing UI strings: cmake --build <dir> --target update_translations
qt_add_translations(imageviewer
    TS_FILE_BASE imageviewer
    TS_FILE_DIR translations
    LUPDATE_OPTIONS -no-obsolete -locations none
)

target_compile_definitions(imageviewer PRIVATE
    IMAGEVIEWER_VERSION="${IMAGEVIEWER_VERSION_STRING}"
    QT_NO_KEYWORDS
)

target_link_libraries(imageviewer PRIVATE
    Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Widgets Qt6::Concurrent
    OpenImageIO::OpenImageIO
    lcms2::lcms2
)

if(MSVC)
    target_compile_options(imageviewer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(imageviewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(imageviewer PROPERTIES
    OUTPUT_NAME imageViewer
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME imageViewer
    MACOSX_BUNDLE_GUI_IDENTIFIER com.cristallumnis.imageviewer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

# Deployment: Qt libraries/plugins next to the executable (Windows, macOS bundle, Linux).
install(TARGETS imageviewer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Apache-2.0 section 4: every redistribution carries the licence and the NOTICE file,
# plus the licence texts of the third-party libraries it ships (third-party/).
# Installed before the deploy script so that macdeployqt's signature covers them.
if(APPLE)
    set(doc_dir imageViewer.app/Contents/Resources)
else()
    set(doc_dir .)
endif()
install(FILES LICENSE NOTICE DESTINATION ${doc_dir})
install(FILES
    packaging/licenses/Qt.txt
    packaging/licenses/LGPL-3.0.txt
    packaging/licenses/GPL-3.0.txt
    DESTINATION ${doc_dir}/third-party
)
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    # vcpkg has installed the dependencies by now (manifest mode runs it at project()).
    file(GLOB port_copyrights LIST_DIRECTORIES false
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
    foreach(copyright IN LISTS port_copyrights)
        get_filename_component(port_dir "${copyright}" DIRECTORY)
        get_filename_component(port "${port_dir}" NAME)
        # vcpkg-cmake, vcpkg-cmake-config, vcpkg-tool-*, ...: build helpers, not shipped.
        if(NOT port MATCHES "^vcpkg-")
            install(FILES "${copyright}" DESTINATION ${doc_dir}/third-party RENAME "${port}.txt")
        endif()
    endforeach()
endif()

set(deploy_options NO_TRANSLATIONS)
if(APPLE AND DEFINED VCPKG_INSTALLED_DIR)
    # macdeployqt resolves @rpath/ dependencies only through the LC_RPATH entries of the
    # binary (its -libpath option is not consulted for them), and the installed executable
    # only has @executable_path/../Frameworks. Point it at the vcpkg libraries as well:
    # macdeployqt copies them into Contents/Frameworks and then deletes this absolute rpath.
    set_property(TARGET imageviewer APPEND PROPERTY
        INSTALL_RPATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
endif()
if(WIN32)
    # MSVC runtime DLLs go next to imageViewer.exe (app-local) instead of the
    # vc_redist installer that windeployqt would add.
    list(APPEND deploy_options NO_COMPILER_RUNTIME)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()
qt_generate_deploy_app_script(TARGET imageviewer
    OUTPUT_SCRIPT deploy_script
    ${deploy_options}
)
install(SCRIPT ${deploy_script})
