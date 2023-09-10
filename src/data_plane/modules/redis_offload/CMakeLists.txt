set(redis_src redis.cpp)

add_library(omni_data_plane_redis SHARED ${redis_src})
target_link_libraries(omni_data_plane_redis PUBLIC ${OMNI_DATA_PLANE_LIB})