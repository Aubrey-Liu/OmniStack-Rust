set(OMNI_LIBRARIES ${OMNI_LIBRARIES} omni_memory PARENT_SCOPE)
add_library(omni_memory include/omnistack/memory/memory.h common.cpp
)
target_include_directories(omni_memory PUBLIC include)