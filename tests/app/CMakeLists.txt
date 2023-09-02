add_executable(perf_test perf_test.cpp)
target_link_libraries(perf_test ${OMNISTACK_USERLIB}_STATIC CLI11::CLI11 ${OMNI_LIBRARIES})

add_executable(http_server http_server.cpp)
target_link_libraries(http_server ${OMNISTACK_USERLIB}_STATIC CLI11::CLI11 ${OMNI_LIBRARIES})
