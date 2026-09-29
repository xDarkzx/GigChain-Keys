qt_add_executable(gigchain_app main.cpp)
# The .exe is named in branding.cmake; on Windows a GUI app (no console).
set_target_properties(gigchain_app PROPERTIES OUTPUT_NAME ${PRODUCT_EXECUTABLE} WIN32_EXECUTABLE $<BOOL:${WIN32}>)
# Its icon, from branding.cmake.
if(WIN32)
    if(NOT EXISTS "${PRODUCT_ICON}")
        message(FATAL_ERROR "The app's icon ${PRODUCT_ICON} is missing: make it with tools/make-icon.ps1")
    endif()
    configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(gigchain_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/app.rc APPEND PROPERTY OBJECT_DEPENDS "${PRODUCT_ICON}")
endif()
target_link_libraries(gigchain_app PRIVATE gigchain_ui gigchain_uiplugin Qt6::Gui Qt6::Quick Qt6::QuickControls2)
gigchain_target_defaults(gigchain_app)
gigchain_version_resource(gigchain_app "${PRODUCT_DESCRIPTION}")
# The app reads new plugins through the scanner, built next to it.
add_dependencies(gigchain_app gigchain_scanner)

# ---------------------------------------------------------------- install
# `cmake --install` lays out the app as it ships, everything in one folder:
# the app, its scanner, the libraries they load (Qt's own are added by
# windeployqt, see tools/package.ps1), the Visual C++ runtime (app-local, no
# separate installer) and the licences. tools/package.ps1 packs it.
install(TARGETS gigchain_app gigchain_scanner RUNTIME DESTINATION .)
if(WIN32) # (the Linux and macOS packages: cross-platform piece 3)
    install(FILES "$<FILTER:$<TARGET_RUNTIME_DLLS:gigchain_app>,EXCLUDE,/Qt6[^/]*\\.dll$>" DESTINATION .)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE) # part of Windows 10 and 11
    include(InstallRequiredSystemLibraries)
endif()
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION . RENAME LICENSE.txt)
configure_file(${PROJECT_SOURCE_DIR}/installer/THIRD-PARTY-NOTICES.txt.in ${CMAKE_BINARY_DIR}/installer/THIRD-PARTY-NOTICES.txt @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/installer/THIRD-PARTY-NOTICES.txt DESTINATION .)
install(FILES ${vst3sdk_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME vst3sdk.txt)
foreach(_port rtaudio rtmidi tl-expected)
    install(FILES ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${_port}/copyright
            DESTINATION licenses RENAME ${_port}.txt)
endforeach()
# The installer's names (installer/setup.iss includes it) and its note to testers.
configure_file(${PROJECT_SOURCE_DIR}/installer/branding.iss.in ${CMAKE_BINARY_DIR}/installer/branding.iss @ONLY)
configure_file(${PROJECT_SOURCE_DIR}/installer/ALPHA.txt.in ${CMAKE_BINARY_DIR}/installer/ALPHA.txt @ONLY)
