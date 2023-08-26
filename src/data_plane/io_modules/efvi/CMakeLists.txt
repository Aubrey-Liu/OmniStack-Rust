set(omni_io_module_efvi_src efvi.cpp)
add_library(omni_io_module_efvi SHARED ${omni_io_module_efvi_src})
target_link_libraries(omni_io_module_efvi PUBLIC ${OMNI_DATA_PLANE_LIB})
set_property(TARGET omni_io_module_efvi PROPERTY POSITION_INDEPENDENT_CODE ON)