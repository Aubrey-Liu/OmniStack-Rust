add_executable(module_test sample_module.cpp)
target_link_libraries(module_test PUBLIC omni_data_plane GTest::gtest_main dl)
target_link_options(module_test PUBLIC -Wl,-E)
gtest_discover_tests(module_test)