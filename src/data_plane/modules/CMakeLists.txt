add_library(omni_data_plane_sample_module SHARED ../module/sample_module.cpp)
target_link_libraries(omni_data_plane_sample_module PUBLIC ${OMNI_DATA_PLANE_LIB} -rdynamic)

add_subdirectory(eth_parser)
add_subdirectory(ipv4_parser)
add_subdirectory(ipv4)
add_subdirectory(tcp_parser)
add_subdirectory(udp_parser)
add_subdirectory(tcp)