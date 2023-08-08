set(eth_parser_src eth_parser.cpp)

add_library(omni_data_plane_eth_parser SHARED ${eth_parser_src})

target_link_libraries(omni_data_plane_eth_parser PUBLIC ${OMNI_DATA_PLANE_LIB})