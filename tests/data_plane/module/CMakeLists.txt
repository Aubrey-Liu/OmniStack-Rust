add_executable(module_test sample_module.cpp)
target_link_libraries(module_test PUBLIC -Wl,-E omni_data_plane GTest::gtest_main -Wl,-ldl)
gtest_discover_tests(module_test)