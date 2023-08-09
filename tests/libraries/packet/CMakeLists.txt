add_executable(packet_test packet.cpp)
target_link_libraries(packet_test GTest::gtest_main omni_packet)
gtest_discover_tests(packet_test)