add_executable(token_test token.cpp)
target_link_libraries(token_test GTest::gtest_main omni_memory omni_token)
gtest_discover_tests(token_test)