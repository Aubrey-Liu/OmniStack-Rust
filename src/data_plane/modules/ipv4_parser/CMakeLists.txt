set(ipv4_parser_src ipv4_parser.cpp)

add_library(ipv4_parser SHARED ${ipv4_parser_src})
SET_TARGET_PROPERTIES(ipv4_parser PROPERTIES VERSION 0.1 SOVERSION 1)

target_link_libraries(ipv4_parser PUBLIC ${OMNI_DATA_PLANE_LIB})