set(udp_recver_src udp_recver.cpp)

add_library(omni_data_plane_udp_recver SHARED ${udp_recver_src})

target_link_libraries(omni_data_plane_udp_recver PUBLIC ${OMNI_DATA_PLANE_LIB})