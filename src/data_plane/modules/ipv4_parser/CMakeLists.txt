set(ipv4_parser_src ipv4_parser.cpp)

add_library(omni_data_plane_ipv4_parser SHARED ${ipv4_parser_src})

target_link_libraries(omni_data_plane_ipv4_parser PUBLIC ${OMNI_DATA_PLANE_LIB})