# The app's sound pack: the recordings its own instruments play, made from
# free recordings (their licences: CREDITS.txt in each folder).
#   piano/  Piano Pro's pianos and electric pianos (the Salamander concert
#           grand, VCSL's Knight upright, a Rhodes, a Wurlitzer, a CP80
#           electric grand, a Pianet): tools/make-piano-pack.py
#   pads/   Pads Pro's string section and piano: tools/make-pad-samples.py
# Downloaded once (checked against its SHA-256), kept in
# GIGCHAIN_SAMPLE_CACHE, and unpacked into samples/ next to the app.
# Without it (offline, a failed download) the build goes on with a warning
# and those instruments play their synthesised stand-ins instead, saying why.

set(GIGCHAIN_PIANO_PACK "sound-pack-1")
set(GIGCHAIN_PIANO_ZIP_URL "https://github.com/xDarkzx/GigChain-Keys/releases/download/${GIGCHAIN_PIANO_PACK}/${GIGCHAIN_PIANO_PACK}.zip")
set(GIGCHAIN_PIANO_ZIP_SHA256 "af95b3b2f8c759c6206cf958e827063ad44a941b828f48a23846da7fa737c5f5")
set(GIGCHAIN_SAMPLE_CACHE "${PROJECT_SOURCE_DIR}/../piano-samples" CACHE PATH
    "Where the downloaded sound pack is kept between builds")
option(GIGCHAIN_FETCH_SAMPLES "Download the app's sound pack (about 110 MB, once)" ON)

function(gigchain_fetch_piano_samples)
    set(dest "${CMAKE_BINARY_DIR}/samples")
    if(EXISTS "${dest}/${GIGCHAIN_PIANO_PACK}.txt")
        return()
    endif()
    if(NOT GIGCHAIN_FETCH_SAMPLES)
        message(STATUS "Sound pack: not fetched (GIGCHAIN_FETCH_SAMPLES is OFF); Piano Pro and Pads Pro play their stand-ins")
        return()
    endif()
    set(zip "${GIGCHAIN_SAMPLE_CACHE}/${GIGCHAIN_PIANO_PACK}.zip")
    if(EXISTS "${zip}")
        file(SHA256 "${zip}" have)
        if(NOT have STREQUAL GIGCHAIN_PIANO_ZIP_SHA256)
            message(WARNING "Sound pack: ${zip} is not the expected download (SHA-256 ${have}, expected "
                            "${GIGCHAIN_PIANO_ZIP_SHA256}); move it aside to download it again. Piano Pro and Pads Pro play their stand-ins.")
            return()
        endif()
    else()
        message(STATUS "Sound pack: downloading ${GIGCHAIN_PIANO_ZIP_URL} (about 110 MB, once) to ${GIGCHAIN_SAMPLE_CACHE}")
        file(MAKE_DIRECTORY "${GIGCHAIN_SAMPLE_CACHE}")
        file(DOWNLOAD "${GIGCHAIN_PIANO_ZIP_URL}" "${zip}.part"
             EXPECTED_HASH SHA256=${GIGCHAIN_PIANO_ZIP_SHA256} STATUS status TLS_VERIFY ON)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            list(GET status 1 why)
            message(WARNING "Sound pack: the download failed (${why}). Piano Pro and Pads Pro play their stand-ins until it is fetched.")
            return()
        endif()
        file(RENAME "${zip}.part" "${zip}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${dest}")
    # Which pack is there (a newer one replaces it: its own marker).
    file(WRITE "${dest}/${GIGCHAIN_PIANO_PACK}.txt" "${GIGCHAIN_PIANO_ZIP_URL}\nSHA-256: ${GIGCHAIN_PIANO_ZIP_SHA256}\n")
    message(STATUS "Sound pack: ready in ${dest}")
endfunction()

gigchain_fetch_piano_samples()
