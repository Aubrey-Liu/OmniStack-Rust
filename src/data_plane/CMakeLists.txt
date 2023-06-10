set(OMNI_DATA_PLANE_LIB omni_data_plane)

set(OLD_ENGINE_SRCS ${ENGINE_SRCS})
set(OLD_GRAPH_SRCS ${GRAPH_SRCS})
set(OLD_IO_SRCS ${IO_SRCS})
set(OLD_MODULE_SRCS ${MODULE_SRCS})

set(ENGINE_SRCS engine/engine.cpp)
set(GRAPH_SRCS graph/graph.cpp)
set(IO_SRCS io/io.cpp)
set(MODULE_SRCS module/module.cpp)

add_library(${OMNI_DATA_PLANE_LIB} ${ENGINE_SRCS} ${GRAPH_SRCS} ${IO_SRCS} ${MODULE_SRCS})
target_include_directories(${OMNI_DATA_PLANE_LIB} PUBLIC include)
message(STATUS "${OMNI_LIBRARIES}")
target_link_libraries(${OMNI_DATA_PLANE_LIB} PUBLIC ${OMNI_LIBRARIES})


set(ENGINE_SRCS ${OLD_ENGINE_SRCS})
set(GRAPH_SRCS ${OLD_GRAPH_SRCS})
set(IO_SRCS ${OLD_IO_SRCS})
set(MODULE_SRCS ${OLD_MODULE_SRCS})

