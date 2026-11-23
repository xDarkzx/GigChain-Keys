# Fails when CITATION.cff (what GitHub's "Cite this repository" and archives
# read) names another version than branding.cmake: it was left at 0.5.0 for
# six releases. CITATION and VERSION are passed in.
file(STRINGS "${CITATION}" line REGEX "^version:")
string(REGEX REPLACE "^version:[ \t]*\"?([^\"]*)\"?[ \t]*$" "\\1" cited "${line}")
if(NOT cited STREQUAL VERSION)
    message(FATAL_ERROR "CITATION.cff says version '${cited}', branding.cmake '${VERSION}': set its version and "
                        "date-released to the release's (docs/RELEASING.md, step 2)")
endif()
message("CITATION.cff names this version: ${VERSION}")
