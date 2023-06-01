set(CMAKE_MINIMUM_REQUIRED_VERSION 3.25)
cmake_minimum_required(VERSION 3.25)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)

project(OmniStack)

set(OMNISTACK_EXEC omnistack)
set(OMNISTACK_USERLIB omniuser)

add_subdirectory(tool)
add_subdirectory(application)
add_subdirectory(src)
