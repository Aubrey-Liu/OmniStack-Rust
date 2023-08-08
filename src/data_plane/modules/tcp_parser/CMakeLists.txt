set(tcp_parser_src tcp_parser.cpp)

add_library(omni_data_plane_tcp_parser SHARED ${tcp_parser_src})

target_link_libraries(omni_data_plane_tcp_parser PUBLIC ${OMNI_DATA_PLANE_LIB})