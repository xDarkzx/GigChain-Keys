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
# The Mac: "GigChain Keys.app" with its Info.plist (bundle id, macOS 13, the
# microphone's reason) and its icon, made from branding/app-icon.png.
if(APPLE)
    set(_icns ${CMAKE_CURRENT_BINARY_DIR}/app.icns)
    set(_iconset ${CMAKE_CURRENT_BINARY_DIR}/app.iconset)
    set(_icon ${PRODUCT_BRAND_DIR}/app-icon.png) # 256 x 256
    add_custom_command(OUTPUT ${_icns}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_iconset}
        COMMAND sips -z 16 16 ${_icon} --out ${_iconset}/icon_16x16.png
        COMMAND sips -z 32 32 ${_icon} --out ${_iconset}/icon_16x16@2x.png
        COMMAND sips -z 32 32 ${_icon} --out ${_iconset}/icon_32x32.png
        COMMAND sips -z 64 64 ${_icon} --out ${_iconset}/icon_32x32@2x.png
        COMMAND sips -z 128 128 ${_icon} --out ${_iconset}/icon_128x128.png
        COMMAND sips -z 256 256 ${_icon} --out ${_iconset}/icon_128x128@2x.png
        COMMAND sips -z 256 256 ${_icon} --out ${_iconset}/icon_256x256.png
        COMMAND iconutil -c icns ${_iconset} -o ${_icns}
        DEPENDS ${_icon}
        COMMENT "The app's Mac icon")
    target_sources(gigchain_app PRIVATE ${_icns})
    set_source_files_properties(${_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(gigchain_app PROPERTIES
        OUTPUT_NAME "${PRODUCT_NAME}"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${PRODUCT_BRAND_DIR}/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER ${PRODUCT_BUNDLE_ID}
        MACOSX_BUNDLE_BUNDLE_NAME "${PRODUCT_NAME}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PRODUCT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PRODUCT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "${PRODUCT_COPYRIGHT}")
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
if(APPLE) # the scanner is inside the bundle already (see src/scanner)
    install(TARGETS gigchain_app BUNDLE DESTINATION .)
else()
    install(TARGETS gigchain_app gigchain_scanner RUNTIME DESTINATION .)
endif()
if(WIN32) # (the Linux and macOS packages: cross-platform piece 3)
    install(FILES "$<FILTER:$<TARGET_RUNTIME_DLLS:gigchain_app>,EXCLUDE,/Qt6[^/]*\\.dll$>" DESTINATION .)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE) # part of Windows 10 and 11
    include(InstallRequiredSystemLibraries)
endif()
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION . RENAME LICENSE.txt)
# The notices name the files this system's app carries.
if(WIN32)
    set(NOTICE_QT_FILES "the separate Qt6*.dll files and the qml and plugin\n  folders next to the app")
    set(NOTICE_FFMPEG_FILES "the separate av*.dll and sw*.dll files")
    set(NOTICE_RUNTIME "Microsoft Visual C++ Runtime (msvcp140.dll, vcruntime140*.dll, concrt140.dll)\n  Redistributed under the Microsoft Visual Studio licence terms.\n\n")
elseif(APPLE)
    set(NOTICE_QT_FILES "the Qt frameworks and plugins inside the app\n  (Contents/Frameworks, Contents/PlugIns and Contents/Resources/qml)")
    set(NOTICE_FFMPEG_FILES "the libav*.dylib and libsw*.dylib files in the app's\n  Contents/Frameworks")
    set(NOTICE_RUNTIME "")
else()
    set(NOTICE_QT_FILES "the separate libQt6*.so libraries and the qml and plugin folders\n  the app loads")
    set(NOTICE_FFMPEG_FILES "the separate libav*.so and libsw*.so libraries")
    set(NOTICE_RUNTIME "")
endif()
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
