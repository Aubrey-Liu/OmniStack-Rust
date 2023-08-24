set(io_module_dpdk_src dpdk.cpp)

add_library(io_module_dpdk SHARED ${io_module_dpdk_src})

target_link_libraries(io_module_dpdk PUBLIC ${OMNI_DATA_PLANE_LIB})
target_compile_options(io_module_dpdk PUBLIC -fPIC)