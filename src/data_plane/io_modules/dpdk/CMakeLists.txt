set(omni_io_module_dpdk_src dpdk.cpp)
add_library(omni_io_module_dpdk SHARED ${omni_io_module_dpdk_src})
target_link_libraries(omni_io_module_dpdk PUBLIC ${OMNI_DATA_PLANE_LIB})

if (${OMNIMEM_BACKEND_DPDK})
    target_link_libraries(omni_io_module_dpdk PUBLIC PkgConfig::DPDK)
endif()

target_compile_options(omni_io_module_dpdk PUBLIC -fPIC)
set_property(TARGET omni_io_module_dpdk PROPERTY POSITION_INDEPENDENT_CODE ON)