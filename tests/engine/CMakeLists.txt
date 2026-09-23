openstage_add_test(tst_fake_engine SOURCES tst_fake_engine.cpp LIBS openstage::engine)
# White-box tests of engine internals.
openstage_add_test(tst_render_graph SOURCES tst_render_graph.cpp LIBS openstage::engine)
target_include_directories(tst_render_graph PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/internal)
