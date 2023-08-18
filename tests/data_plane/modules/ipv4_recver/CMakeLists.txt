add_executable(module_ipv4_recver_test ipv4_recver.cpp)
target_link_libraries(module_ipv4_recver_test PUBLIC -Wl,-E omni_data_plane GTest::gtest_main -Wl,-ldl)
gtest_discover_tests(module_ipv4_recver_test)