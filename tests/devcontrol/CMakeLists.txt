# The live test control's tests (dev presets only: docs/testing/live-test-control.md).
gigchain_add_test(tst_dev_protocol SOURCES tst_dev_protocol.cpp LIBS gigchain_devcontrol)
gigchain_add_test(tst_gck_ctl SOURCES tst_gck_ctl.cpp LIBS gigchain_devcontrol)
add_dependencies(tst_gck_ctl gck_ctl) # (it runs the tool)
gigchain_add_test(tst_dev_control SOURCES tst_dev_control.cpp DevClient.h LIBS gigchain_devcontrol gigchain_uiplugin Qt6::QuickControls2)
