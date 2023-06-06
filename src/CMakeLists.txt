add_subdirectory(libraries)
add_subdirectory(control_plane)
add_subdirectory(data_plane)
add_subdirectory(user_library)

target_link_libraries(${OMNISTACK_USERLIB} PUBLIC ${OMNI_LIBRARIES})

add_executable(${OMNISTACK_EXEC} app/main.cpp)
target_link_libraries(${OMNISTACK_EXEC} PUBLIC ${OMNI_CONTROL_PLANE_LIB})
target_link_libraries(${OMNISTACK_EXEC} PUBLIC ${OMNI_DATA_PLANE_LIB})
target_link_libraries(${OMNISTACK_EXEC} PUBLIC ${OMNI_LIBRARIES})
