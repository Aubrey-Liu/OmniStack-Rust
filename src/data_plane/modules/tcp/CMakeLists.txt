add_library(omni_data_plane_tcp_common 
STATIC 
    include/tcp_constant.hpp 
    include/tcp_buffer.hpp 
    include/tcp_congestion_control.hpp 
    include/tcp_state.hpp 
    include/tcp_shared.hpp
)
set_target_properties(omni_data_plane_tcp_common PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(omni_data_plane_tcp_common PUBLIC include)

add_library(omni_data_plane_tcp_flow_cache SHARED tcp_flow_cache.cpp)
target_link_libraries(omni_data_plane_tcp_flow_cache PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)

add_library(omni_data_plane_tcp_state_in SHARED tcp_state_in.cpp)
target_link_libraries(omni_data_plane_tcp_state_in PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)