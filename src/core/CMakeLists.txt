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
    internal/Error.cpp
    internal/Model.cpp
    internal/Validation.cpp
    internal/SetlistJson.cpp
    internal/SetlistFile.cpp
    internal/Navigation.cpp
    internal/Editing.cpp
    internal/FileLog.cpp
)
add_library(gigchain::core ALIAS gigchain_core)
target_include_directories(gigchain_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal)
target_link_libraries(gigchain_core PUBLIC Qt6::Core tl::expected)
gigchain_target_defaults(gigchain_core)
