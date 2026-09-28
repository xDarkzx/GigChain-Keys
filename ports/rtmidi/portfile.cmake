# vcpkg's own rtmidi port, pinned to an upstream master commit instead of the
# 6.0.0 release: 6.0.0 crashes the app when a MIDI keyboard is unplugged
# (MidiInWinMM::closePort frees the sysex buffers, returns early, and the
# destructor frees them again; thestk/rtmidi#376). Back to the stock port once
# a release has the fix.

# Upstream uses CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS, which causes issues
# https://github.com/thestk/rtmidi/blob/4.0.0/CMakeLists.txt#L20
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO thestk/rtmidi
    REF 23b8cd5fa6aae239c3468ebb3b76d06310c9a3ca
    SHA512 2b7834a9931ffc15825d808a074ec3b26d25bd045ec54cc3fe14e25995aa48c5f47cd30b267b7d52c2049f0b5ec61cf0873453e0060fa88d3dea2e63dfc8f85f
    HEAD_REF master
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        alsa RTMIDI_API_ALSA
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DRTMIDI_API_JACK=OFF
        -DRTMIDI_BUILD_TESTING=OFF
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_fixup_pkgconfig()

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
