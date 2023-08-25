add_executable(module_eth_sender_test eth_sender.cpp)
target_link_libraries(module_eth_sender_test PUBLIC -Wl,-E omni_data_plane GTest::gtest_main -Wl,-ldl)
gtest_discover_tests(module_eth_sender_test)