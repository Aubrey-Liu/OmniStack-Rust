add_executable(memory_test memory.cpp)
target_link_libraries(memory_test GTest::gtest_main omni_memory)
gtest_discover_tests(memory_test)