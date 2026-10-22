# Piano Pro's recorded pianos: the Kawai grand and the Yamaha upright from
# Versilian Studios' VCSL Keys, CC0 (public domain: github.com/sgossner/VCSL,
# LICENSE CC0-1.0). Downloaded once (checked against its SHA-256), kept in
# GIGCHAIN_SAMPLE_CACHE, and the two pianos unpacked into samples/piano next
# to the app. Without them (offline, a failed download) the build goes on
# with a warning and Piano Pro plays its own FM piano instead, saying why.

set(GIGCHAIN_PIANO_ZIP_URL "https://versilian-studios.com/Distro/VCSL_Keys.zip")
set(GIGCHAIN_PIANO_ZIP_SHA256 "2e91c9aa7b16d936f035963149df1fe4cbd65911116fb3e3ea6daca52e92024b")
set(GIGCHAIN_SAMPLE_CACHE "${PROJECT_SOURCE_DIR}/../piano-samples" CACHE PATH
    "Where the downloaded piano samples are kept between builds")
option(GIGCHAIN_FETCH_SAMPLES "Download Piano Pro's piano samples (about 650 MB, once)" ON)

function(gigchain_fetch_piano_samples)
    set(dest "${CMAKE_BINARY_DIR}/samples/piano")
    if(EXISTS "${dest}/Grand Piano, K.sfz" AND EXISTS "${dest}/Upright Piano, Y.sfz")
        return()
    endif()
    if(NOT GIGCHAIN_FETCH_SAMPLES)
        message(STATUS "Piano samples: not fetched (GIGCHAIN_FETCH_SAMPLES is OFF); Piano Pro plays its FM piano")
        return()
    endif()
    set(zip "${GIGCHAIN_SAMPLE_CACHE}/VCSL_Keys.zip")
    if(EXISTS "${zip}")
        file(SHA256 "${zip}" have)
        if(NOT have STREQUAL GIGCHAIN_PIANO_ZIP_SHA256)
            message(WARNING "Piano samples: ${zip} is not the expected download (SHA-256 ${have}, expected "
                            "${GIGCHAIN_PIANO_ZIP_SHA256}); move it aside to download it again. Piano Pro plays its FM piano.")
            return()
        endif()
    else()
        message(STATUS "Piano samples: downloading ${GIGCHAIN_PIANO_ZIP_URL} (about 650 MB, once) to ${GIGCHAIN_SAMPLE_CACHE}")
        file(MAKE_DIRECTORY "${GIGCHAIN_SAMPLE_CACHE}")
        file(DOWNLOAD "${GIGCHAIN_PIANO_ZIP_URL}" "${zip}.part"
             EXPECTED_HASH SHA256=${GIGCHAIN_PIANO_ZIP_SHA256} STATUS status TLS_VERIFY ON)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            list(GET status 1 why)
            message(WARNING "Piano samples: the download failed (${why}). Piano Pro plays its FM piano until it is fetched.")
            return()
        endif()
        file(RENAME "${zip}.part" "${zip}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${dest}"
         PATTERNS "Grand Piano, K/*" "Grand Piano, K.sfz" "Upright Piano, Y/*" "Upright Piano, Y.sfz")
    file(WRITE "${dest}/LICENSE-VCSL.txt"
         "The piano recordings in this folder (Grand Piano, K and Upright Piano, Y) are from the\n"
         "Versilian Community Sample Library's VCSL Keys by Versilian Studios LLC, released\n"
         "under Creative Commons 0 (CC0 1.0 Universal, public domain dedication):\n"
         "https://creativecommons.org/publicdomain/zero/1.0/\n\n"
         "Source: ${GIGCHAIN_PIANO_ZIP_URL}\n"
         "SHA-256: ${GIGCHAIN_PIANO_ZIP_SHA256}\n"
         "Library: https://github.com/sgossner/VCSL (LICENSE: CC0-1.0)\n")
    message(STATUS "Piano samples: ready in ${dest}")
endfunction()

gigchain_fetch_piano_samples()
