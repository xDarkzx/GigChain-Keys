# Fails when a product name is typed into the source instead of coming from
# branding.cmake (so renaming stays a one-file change). BRAND and OLD_NAMES
# are passed in; the internal code name (lowercase "gigchain", the QML module
# "GigChain.Ui" and its resource path "GigChain/Ui") is not branding and is allowed.
file(GLOB_RECURSE sources "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h" "${SOURCE_DIR}/src/*.qml")
set(found "")
foreach(file IN LISTS sources)
    file(READ "${file}" text)
    string(REPLACE "GigChain.Ui" "" text "${text}")
    string(REPLACE "GigChain_UiPlugin" "" text "${text}")
    string(REPLACE "GigChain/Ui" "" text "${text}")
    foreach(name IN LISTS BRAND OLD_NAMES)
        string(FIND "${text}" "${name}" at)
        if(NOT at EQUAL -1)
            list(APPEND found "${file}: ${name}")
        endif()
    endforeach()
endforeach()
if(found)
    list(JOIN found "\n  " lines)
    message(FATAL_ERROR "Product names typed into the source (use gigchain::branding or Branding.* instead):\n  ${lines}")
endif()
message("No product names in the source: all come from branding.cmake")
