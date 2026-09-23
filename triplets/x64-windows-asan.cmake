# x64-windows with AddressSanitizer, used by the "asan" preset. MSVC refuses to
# link ASan-instrumented objects against uninstrumented static libraries that
# use the same STL containers, so dependencies are instrumented too.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_C_FLAGS "/fsanitize=address")
set(VCPKG_CXX_FLAGS "/fsanitize=address")
