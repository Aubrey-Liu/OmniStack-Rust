add_executable(perf_test perf_test.cpp)
target_link_libraries(perf_test ${OMNISTACK_USERLIB}_STATIC CLI11::CLI11 ${OMNI_LIBRARIES})
