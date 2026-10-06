cmake_minimum_required(VERSION 3.24)

project(imageviewer VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6.11: QRhi (HDR swapchains) is reached through Qt6::GuiPrivate; its API is
# stable enough for us but carries no compatibility promise (pin Qt minor versions).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets Concurrent ShaderTools)
qt_standard_project_setup()

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
    src/viewer.h src/viewer.cpp
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

target_compile_definitions(imageviewer PRIVATE
    IMAGEVIEWER_VERSION="${PROJECT_VERSION}"
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
qt_generate_deploy_app_script(TARGET imageviewer
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
)
install(SCRIPT ${deploy_script})
