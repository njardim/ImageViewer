# Overlay of vcpkg's community triplet (triplets/community/arm64-osx-dynamic.cmake at the
# builtin-baseline) that also pins the macOS deployment target of every dependency.
# Keep VCPKG_OSX_DEPLOYMENT_TARGET equal to CMAKE_OSX_DEPLOYMENT_TARGET in CMakePresets.json.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.0)
