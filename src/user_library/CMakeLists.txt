add_library(${OMNISTACK_USERLIB} SHARED symb.cpp symb_origin.cpp)
set_property(TARGET ${OMNISTACK_USERLIB} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(${OMNISTACK_USERLIB} PUBLIC -DOMNIAPI_DYNAMIC)
target_link_libraries(${OMNISTACK_USERLIB} PUBLIC ${OMNI_LIBRARIES})
target_include_directories(${OMNISTACK_USERLIB} PUBLIC include)

add_library(${OMNISTACK_USERLIB}_STATIC STATIC symb.cpp symb_origin.cpp)
target_link_libraries(${OMNISTACK_USERLIB} PUBLIC ${OMNI_LIBRARIES})
target_include_directories(${OMNISTACK_USERLIB} PUBLIC include)
