INCLUDE(CheckIncludeFiles)
CHECK_INCLUDE_FILES(etherfabric/vi.h IS_EFVI_EXISTS)

set(omni_io_module_efvi_src efvi.cpp)
add_library(omni_io_module_efvi SHARED ${omni_io_module_efvi_src})
target_link_libraries(omni_io_module_efvi PUBLIC ${OMNI_DATA_PLANE_LIB})
set_property(TARGET omni_io_module_efvi PROPERTY POSITION_INDEPENDENT_CODE ON)

if (${IS_EFVI_EXISTS})
    target_compile_definitions(omni_io_module_efvi PRIVATE OMNIDRIVE_EF_VI)
    target_link_libraries(omni_io_module_efvi PRIVATE ciul1)
endif()