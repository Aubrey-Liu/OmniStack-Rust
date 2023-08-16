add_library(${OMNISTACK_USERLIB} SHARED symb.cpp symb_origin.cpp symb.h)
set_property(TARGET ${OMNISTACK_USERLIB} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(${OMNISTACK_USERLIB} PUBLIC -DOMNIAPI_DYNAMIC)

add_library(${OMNISTACK_USERLIB}_STATIC STATIC symb.cpp symb_origin.cpp symb.h)
