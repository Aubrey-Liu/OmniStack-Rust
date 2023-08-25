set(eth_recver_src eth_recver.cpp)
set(eth_sender_src eth_sender.cpp)

add_library(omni_data_plane_eth_recver SHARED ${eth_recver_src})
add_library(omni_data_plane_eth_sender SHARED ${eth_sender_src})

target_link_libraries(omni_data_plane_eth_recver PUBLIC ${OMNI_DATA_PLANE_LIB})
target_link_libraries(omni_data_plane_eth_sender PUBLIC ${OMNI_DATA_PLANE_LIB})