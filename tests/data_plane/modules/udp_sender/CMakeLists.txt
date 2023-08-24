add_executable(module_udp_sender_test udp_sender.cpp)
target_link_libraries(module_udp_sender_test PUBLIC -Wl,-E omni_data_plane GTest::gtest_main -Wl,-ldl)
gtest_discover_tests(module_udp_sender_test)