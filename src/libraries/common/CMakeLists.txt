set(OMNI_LIBRARIES ${OMNI_LIBRARIES} omni_common PARENT_SCOPE)
add_library(omni_common include/omnistack/common/packet.hpp packet.cpp)
target_include_directories(omni_common PUBLIC include)