set(udp_parser_src udp_parser.cpp)

add_library(omni_data_plane_udp_parser SHARED ${udp_parser_src})

target_link_libraries(omni_data_plane_udp_parser PUBLIC ${OMNI_DATA_PLANE_LIB})