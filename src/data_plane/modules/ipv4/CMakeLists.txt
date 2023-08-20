set(ipv4_sender_src ipv4_sender.cpp)
set(ipv4_recver_src ipv4_recver.cpp)

add_library(omni_data_plane_ipv4_sender SHARED ${ipv4_sender_src})
add_library(omni_data_plane_ipv4_recver SHARED ${ipv4_recver_src})

target_link_libraries(omni_data_plane_ipv4_sender PUBLIC ${OMNI_DATA_PLANE_LIB})
target_link_libraries(omni_data_plane_ipv4_recver PUBLIC ${OMNI_DATA_PLANE_LIB})