set(nf_src ids.cpp firewall.cpp nat.cpp)

add_library(omni_data_plane_network_function SHARED ${nf_src})

target_link_libraries(omni_data_plane_network_function PUBLIC ${OMNI_DATA_PLANE_LIB})