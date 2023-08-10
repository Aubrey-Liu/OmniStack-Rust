add_library(omni_packet STATIC packet.cpp)
target_include_directories(omni_packet PUBLIC include)
target_link_libraries(omni_packet PUBLIC omni_memory omni_common)
set(OMNI_LIBRARIES ${OMNI_LIBRARIES} omni_packet PARENT_SCOPE)