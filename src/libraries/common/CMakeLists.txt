add_library(omni_common packet.cpp)
target_include_directories(omni_common PUBLIC include)
set(OMNI_LIBRARIES ${OMNI_LIBRARIES} omni_common PARENT_SCOPE)