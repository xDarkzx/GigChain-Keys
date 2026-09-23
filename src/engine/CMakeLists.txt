add_library(openstage_engine STATIC
    include/openstage/engine/EngineTypes.h
    include/openstage/engine/IEngine.h
    include/openstage/engine/FakeEngineFactory.h
    internal/FakeEngine.h
    internal/FakeEngine.cpp
)
add_library(openstage::engine ALIAS openstage_engine)
target_include_directories(openstage_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal)
target_link_libraries(openstage_engine PUBLIC openstage::core)
openstage_target_defaults(openstage_engine)
