set(ipv4_sender_src ipv4_sender.cpp)

add_library(omni_data_plane_ipv4_sender SHARED ${ipv4_sender_src})

target_link_libraries(omni_data_plane_ipv4_sender PUBLIC ${OMNI_DATA_PLANE_LIB})