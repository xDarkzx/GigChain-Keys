add_library(gigchain_core STATIC
    include/gigchain/core/Error.h
    include/gigchain/core/Ids.h
    include/gigchain/core/Limits.h
    include/gigchain/core/Model.h
    include/gigchain/core/Validation.h
    include/gigchain/core/SetlistJson.h
    include/gigchain/core/SetlistFile.h
    include/gigchain/core/Navigation.h
    include/gigchain/core/Editing.h
    include/gigchain/core/FileLog.h
    include/gigchain/core/Checks.h
    internal/Error.cpp
    internal/Model.cpp
    internal/Validation.cpp
    internal/SetlistJson.cpp
    internal/SetlistFile.cpp
    internal/Navigation.cpp
    internal/Editing.cpp
    include/gigchain/core/Chart.h
    internal/Chart.cpp
    include/gigchain/core/Sections.h
    internal/Sections.cpp
    internal/FileLog.cpp
    internal/Checks.cpp
)
add_library(gigchain::core ALIAS gigchain_core)
# gigchain/core/Branding.h, generated from branding.cmake.
configure_file(Branding.h.in ${CMAKE_BINARY_DIR}/generated/gigchain/core/Branding.h @ONLY)
target_include_directories(gigchain_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal)
target_link_libraries(gigchain_core PUBLIC Qt6::Core tl::expected)
gigchain_target_defaults(gigchain_core)
