add_library(openstage_core STATIC
    include/openstage/core/Error.h
    include/openstage/core/Ids.h
    include/openstage/core/Limits.h
    include/openstage/core/Model.h
    include/openstage/core/Validation.h
    include/openstage/core/SetlistJson.h
    include/openstage/core/SetlistFile.h
    internal/Error.cpp
    internal/Model.cpp
    internal/Validation.cpp
    internal/SetlistJson.cpp
    internal/SetlistFile.cpp
)
add_library(openstage::core ALIAS openstage_core)
target_include_directories(openstage_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal)
target_link_libraries(openstage_core PUBLIC Qt6::Core tl::expected)
openstage_target_defaults(openstage_core)
