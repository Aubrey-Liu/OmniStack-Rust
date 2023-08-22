add_executable(module_udp_recver_test udp_recver.cpp)
target_link_libraries(module_udp_recver_test PUBLIC -Wl,-E omni_data_plane GTest::gtest_main -Wl,-ldl)
gtest_discover_tests(module_udp_recver_test)