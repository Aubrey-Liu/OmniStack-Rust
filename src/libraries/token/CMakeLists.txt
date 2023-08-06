set(OMNI_LIBRARIES ${OMNI_LIBRARIES} omni_token PARENT_SCOPE)
add_library(omni_token token.cpp)
target_include_directories(omni_token PUBLIC include)
target_link_libraries(omni_token PUBLIC omni_memory)