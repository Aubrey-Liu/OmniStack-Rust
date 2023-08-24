set(udp_recver_src udp_recver.cpp)
set(udp_sender_src udp_sender.cpp)

add_library(omni_data_plane_udp_recver SHARED ${udp_recver_src})
add_library(omni_data_plane_udp_sender SHARED ${udp_sender_src})

target_link_libraries(omni_data_plane_udp_recver PUBLIC ${OMNI_DATA_PLANE_LIB})
target_link_libraries(omni_data_plane_udp_sender PUBLIC ${OMNI_DATA_PLANE_LIB})