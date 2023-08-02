set(OMNI_LIBRARIES ${OMNI_LIBRARIES} omni_memory PARENT_SCOPE)
add_library(omni_memory include/omnistack/memory/memory.h common.cpp
)
target_include_directories(omni_memory PUBLIC include)
if (${OMNIMEM_BACKEND_DPDK})
    target_link_libraries(omni_memory PUBLIC PkgConfig::DPDK)
endif()
target_link_libraries(omni_memory PUBLIC numa)