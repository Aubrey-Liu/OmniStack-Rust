set(eth_recver_src eth_recver.cpp)

add_library(omni_data_plane_eth_recver SHARED ${eth_recver_src})

target_link_libraries(omni_data_plane_eth_recver PUBLIC ${OMNI_DATA_PLANE_LIB})