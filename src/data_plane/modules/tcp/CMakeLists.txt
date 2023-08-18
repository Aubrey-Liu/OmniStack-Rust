SET(OMNI_TCP_OPTION_MSS ON CACHE BOOL "Enable TCP option MSS")
SET(OMNI_TCP_OPTION_WSOPT ON CACHE BOOL "Enable TCP option WSOPT")
SET(OMNI_TCP_OPTION_TSPOT ON CACHE BOOL "Enable TCP option TSPOT")

if(OMNI_TCP_OPTION_MSS)
    add_compile_definitions(OMNI_TCP_OPTION_MSS)
endif()
if(OMNI_TCP_OPTION_WSOPT)
    add_compile_definitions(OMNI_TCP_OPTION_WSOPT)
endif()
if(OMNI_TCP_OPTION_TSPOT)
    add_compile_definitions(OMNI_TCP_OPTION_TSPOT)
endif()

add_library(omni_data_plane_tcp_common 
STATIC 
    include/omnistack/tcp_common/tcp_constant.hpp 
    include/omnistack/tcp_common/tcp_buffer.hpp 
    include/omnistack/tcp_common/tcp_congestion_control.hpp 
    include/omnistack/tcp_common/tcp_state.hpp 
    include/omnistack/tcp_common/tcp_events.hpp
    include/omnistack/tcp_common/tcp_shared.hpp
)
set_target_properties(omni_data_plane_tcp_common PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(omni_data_plane_tcp_common PUBLIC include)

add_library(omni_data_plane_tcp_flow_cache_in SHARED tcp_flow_cache_in.cpp)
target_link_libraries(omni_data_plane_tcp_flow_cache_in PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)

add_library(omni_data_plane_tcp_flow_cache_out SHARED tcp_flow_cache_out.cpp)
target_link_libraries(omni_data_plane_tcp_flow_cache_out PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)

add_library(omni_data_plane_tcp_state_in SHARED tcp_state_in.cpp)
target_link_libraries(omni_data_plane_tcp_state_in PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)

add_library(omni_data_plane_tcp_state_out SHARED tcp_state_out.cpp)
target_link_libraries(omni_data_plane_tcp_state_out PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)

add_library(omni_data_plane_tcp_data_in SHARED tcp_data_in.cpp)
target_link_libraries(omni_data_plane_tcp_data_in PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)

add_library(omni_data_plane_tcp_send SHARED tcp_send.cpp)
target_link_libraries(omni_data_plane_tcp_send PUBLIC ${OMNI_DATA_PLANE_LIB} omni_data_plane_tcp_common)