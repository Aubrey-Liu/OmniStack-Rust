add_executable(module_tcp_parser_test tcp_parser.cpp)
target_link_libraries(module_tcp_parser_test PUBLIC -Wl,-E omni_data_plane GTest::gtest_main -Wl,-ldl)
gtest_discover_tests(module_tcp_parser_test)