set(CMAKE_MINIMUM_REQUIRED_VERSION 3.25)
cmake_minimum_required(VERSION 3.25)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
    
set(OMNIMEM_BACKEND_DPDK OFF CACHE BOOL "Use DPDK as memory backend")
if (${OMNIMEM_BACKEND_DPDK})
    add_compile_definitions(OMNIMEM_BACKEND_DPDK)
    pkg_search_module(DPDK REQUIRED libdpdk IMPORTED_TARGET GLOBAL)
endif()

project(OmniStack)

set(OMNISTACK_EXEC omnistack)
set(OMNISTACK_USERLIB omniuser)

add_subdirectory(tool)
add_subdirectory(application)
add_subdirectory(src)
