set(speed_test_src speed_test.cpp)

add_library(omni_data_plane_speed_test SHARED ${speed_test_src})

target_link_libraries(omni_data_plane_speed_test PUBLIC ${OMNI_DATA_PLANE_LIB})